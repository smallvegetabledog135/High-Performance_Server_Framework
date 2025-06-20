#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    
#include <stdarg.h>   
#include <unistd.h>    
#include <sys/time.h>  
#include <time.h>     
#include <fcntl.h>     
#include <errno.h>     
#include <sys/socket.h> 

#include <cstring>   

#include <sys/ioctl.h> 
#include <arpa/inet.h>
#include <pthread.h>  

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"  
#include "ngx_logiccomm.h"  
#include "ngx_c_lockmutex.h"  

#include "library_manager.h"  

//定义成员函数指针
typedef bool (CLogicSocket::*handler)(  lpngx_connection_t pConn,      //连接池中连接的指针
                                        LPSTRUC_MSG_HEADER pMsgHeader,  //消息头指针
                                        char *pPkgBody,                 //包体指针
                                        unsigned short iBodyLength);    //包体长度

//用来保存成员函数指针的数组
static const handler statusHandler[] = 
{
    //数组前5个元素，增加基本服务器功能
    &CLogicSocket::_HandlePing,                             //【0】：心跳包的实现
    NULL,                                                   //【1】：下标从0开始
    NULL,                                                   //【2】：下标从0开始
    NULL,                                                   //【3】：下标从0开始
    NULL,                                                   //【4】：下标从0开始
 
    //开始处理具体的业务逻辑
    &CLogicSocket::_HandleRegister,                         //【5】：实现具体的注册功能
    &CLogicSocket::_HandleLogIn,                            //【6】：实现具体的登录功能

    // 图书馆管理系统请求处理函数
    & CLogicSocket::_HandleAddBook,                          //【1001】：添加书籍
    & CLogicSocket::_HandleRemoveBook,                       //【1002】：移除书籍
    & CLogicSocket::_HandleCheckOutBook,                     //【1003】：借出书籍
    & CLogicSocket::_HandleReturnBook,                       //【1004】：归还书籍
    & CLogicSocket::_HandleListBooks                         //【1005】：列出所有书籍
};
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler) //整个命令有多少个，编译时即可知道

//构造函数
CLogicSocket::CLogicSocket() : m_libraryManager("localhost", "root", "12345678", "library_db")
{
}
//析构函数
CLogicSocket::~CLogicSocket()
{
}

bool CLogicSocket::Initialize()
{        
    bool bParentInit = CSocekt::Initialize();  //调用父类的同名函数
    return bParentInit;
}

// 添加一个发送带包体的响应函数  
void CLogicSocket::SendPkgWithBodyToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode, const char* body, int bodyLen)
{
    CMemory* p_memory = CMemory::GetInstance();
    CCRC32* p_crc32 = CCRC32::GetInstance();

    // 分配内存：消息头 + 包头 + 包体  
    char* p_sendbuf = (char*)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + bodyLen, false);
    char* p_tmpbuf = p_sendbuf;

    // 复制消息头  
    memcpy(p_tmpbuf, pMsgHeader, m_iLenMsgHeader);
    p_tmpbuf += m_iLenMsgHeader;

    // 设置包头  
    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf;
    pPkgHeader->msgCode = htons(iMsgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + bodyLen);

    // 复制包体并计算CRC32  
    if (body != NULL && bodyLen > 0)
    {
        memcpy(p_tmpbuf + m_iLenPkgHeader, body, bodyLen);
        pPkgHeader->crc32 = htonl(p_crc32->Get_CRC((unsigned char*)(p_tmpbuf + m_iLenPkgHeader), bodyLen));
    }
    else
    {
        pPkgHeader->crc32 = 0;
    }

    msgSend(p_sendbuf);
    return;
}

//处理收到的数据包，由线程池来调用本函数
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{     
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()开始处理收到的数据包");  
         
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                  //消息头
    LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+m_iLenMsgHeader); //包头
    void  *pPkgBody;                                                              //指向包体的指针
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen); 
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); //消息代码拿出来                           //客户端指明的包宽度【包头+包体】

    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()包头解析: msgCode=%d, pkgLen=%d",   
                     imsgCode, pkglen);  

    if(m_iLenPkgHeader == pkglen)
    {
        //没有包体，只有包头
		ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()包中只有包头，无包体");  
        if(pPkgHeader->crc32 != 0) //只有包头的crc值给0  
        {  
            ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::threadRecvProcFunc()包头CRC错误，丢弃数据");  
            return; //crc错，直接丢弃  
        }  
		pPkgBody = NULL;
    }
    else 
	{
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()包中有包体，包体长度=%d", pkglen-m_iLenPkgHeader);  
        
        //有包体，走到这里
		pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);		          //针对4字节的数据，网络序转主机序
		pPkgBody = (void *)(pMsgBuf+m_iLenMsgHeader+m_iLenPkgHeader); //跳过消息头 以及 包头 ，指向包体

		int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody,pkglen-m_iLenPkgHeader); //计算纯包体的crc值
		if(calccrc != pPkgHeader->crc32) //服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较  
        {  
            ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误[服务器:%d/客户端:%d]，丢弃数据!",calccrc,pPkgHeader->crc32);  
            ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::threadRecvProcFunc()包体CRC错误[服务器:%d/客户端:%d]，丢弃数据!", calccrc, pPkgHeader->crc32);  
            return; //crc错，直接丢弃  
        }  
        else  
        {  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()包体CRC校验正确[服务器:%d/客户端:%d]", calccrc, pPkgHeader->crc32);  
        }        
	}

    //unsigned short imsgCode = ntohs(pPkgHeader->msgCode); //消息代码拿出来
    lpngx_connection_t p_Conn = pMsgHeader->pConn;        //消息头中藏着连接池中连接的指针

    if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)   
    {
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::threadRecvProcFunc()连接序号不一致，可能客户端已断开，丢弃数据");  
        return;
    }

	if(imsgCode >= AUTH_TOTAL_COMMANDS) //无符号数不可能<0
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!",imsgCode); //恶意倾向或者错误倾向的包，打印出来 
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::threadRecvProcFunc()消息码超出范围，imsgCode=%d，丢弃数据", imsgCode);  
        return; //恶意包或者错误包
    }

    if(statusHandler[imsgCode] == NULL)
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); //恶意倾向或者错误倾向的包，打印出来  
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::threadRecvProcFunc()消息码没有对应的处理函数，imsgCode=%d，丢弃数据", imsgCode);  
        return;  //没有相关的处理函数
    }

    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()调用消息处理函数，msgCode=%d，连接fd=%d",   
                     imsgCode, p_Conn->fd); 
    (this->*statusHandler[imsgCode])(p_Conn,pMsgHeader,(char *)pPkgBody,pkglen-m_iLenPkgHeader);
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::threadRecvProcFunc()消息处理完毕，msgCode=%d", imsgCode);  
    return;	
}

//心跳包检测时间到，检测心跳包是否超时，本函数是子类函数，实现具体的判断动作
void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::procPingTimeOutChecking()开始检测心跳包超时，连接fd=%d",   
                     tmpmsg->pConn ? tmpmsg->pConn->fd : -1);  
    
    CMemory *p_memory = CMemory::GetInstance();

    if(tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence) //此连接没断
    {
        lpngx_connection_t p_Conn = tmpmsg->pConn;

        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::procPingTimeOutChecking()连接仍然有效，上次心跳时间=%d，当前时间=%d，连接fd=%d",   
                         p_Conn->lastPingTime, cur_time, p_Conn->fd);  


        if(/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick == 1) 
        {
            ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::procPingTimeOutChecking()超时踢出功能已开启，关闭连接，fd=%d", p_Conn->fd);  
            
            zdClosesocketProc(p_Conn); 
        }            
        else if( (cur_time - p_Conn->lastPingTime ) > (m_iWaitTime*3+10) ) 
        {           
            ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::procPingTimeOutChecking()心跳包超时，关闭连接，fd=%d，超时时间=%d秒",   
                             p_Conn->fd, (cur_time - p_Conn->lastPingTime)); 
            zdClosesocketProc(p_Conn); 
        }   
        p_memory->FreeMemory(tmpmsg);//内存释放
    }
    else //此连接断开
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::procPingTimeOutChecking()连接已断开，不做处理");  
        p_memory->FreeMemory(tmpmsg);//内存释放
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::procPingTimeOutChecking()心跳包检测完毕");  
    return;
}

//发送没有包体的数据包给客户端
void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader,unsigned short iMsgCode)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::SendNoBodyPkgToClient()开始发送无包体数据包，msgCode=%d，连接fd=%d",   
                     iMsgCode, pMsgHeader->pConn ? pMsgHeader->pConn->fd : -1);  
    
    CMemory  *p_memory = CMemory::GetInstance();

    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader,false);
    char *p_tmpbuf = p_sendbuf;
    
	memcpy(p_tmpbuf,pMsgHeader,m_iLenMsgHeader);
	p_tmpbuf += m_iLenMsgHeader;

    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf;	  //要发送出去的包的包头	
    pPkgHeader->msgCode = htons(iMsgCode);	
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader); 
	pPkgHeader->crc32 = 0;	
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::SendNoBodyPkgToClient()数据包准备完毕，开始发送");  
    	
    msgSend(p_sendbuf);
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::SendNoBodyPkgToClient()数据包发送完毕");  
    return;
}

//----------------------------------------------------------------------------------------------------------
//处理各种业务逻辑
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRegister()开始处理注册请求，连接fd=%d", pConn->fd);  
    
    //判断包体的合法性
    if(pPkgBody == NULL)    
    {        
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::_HandleRegister()包体为空，丢弃请求，连接fd=%d", pConn->fd);  
        return false;
    }
		    
    int iRecvLen = sizeof(STRUCT_REGISTER); 
    if(iRecvLen != iBodyLength) //发送过来的结构大小不对，认为是恶意包，不处理
    {     
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::_HandleRegister()包体长度不匹配，期望=%d，实际=%d，连接fd=%d",   
                         iRecvLen, iBodyLength, pConn->fd);  
        return false; 
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRegister()包体验证通过，开始处理注册数据");  
  
  
    CLock lock(&pConn->logicPorcMutex);
    
    //取得了整个发送过来的数据
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody; 
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);          //传输之前主机网络序，收到后网络转主机序
    p_RecvInfo->username[sizeof(p_RecvInfo->username)-1]=0;//防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。 
    p_RecvInfo->password[sizeof(p_RecvInfo->password)-1]=0;//防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。 

    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleRegister()收到注册请求，用户名=%s，类型=%d，连接fd=%d",   
                     p_RecvInfo->username, p_RecvInfo->iType, pConn->fd);  

	LPCOMM_PKG_HEADER pPkgHeader;	
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(STRUCT_REGISTER);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRegister()准备响应数据，包体长度=%d", iSendLen);  

    //分配要发送出去的包的内存
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader+iSendLen,false);//准备发送的格式，这里是消息头+包头+包体
    //填充消息头
    memcpy(p_sendbuf,pMsgHeader,m_iLenMsgHeader);                   //消息头拷贝到这里
    //填充包头
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf+m_iLenMsgHeader);    //指向包头
    pPkgHeader->msgCode = _CMD_REGISTER;	                        //消息代码，可以统一在ngx_logiccomm.h中定义
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序 
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);        //整个包的尺寸【包头+包体尺寸】 
    //填充包体
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf+m_iLenMsgHeader+m_iLenPkgHeader);	//跳过消息头，跳过包头，就是包体
    //填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；
    
    //包体内容全部确定好后，计算包体的crc32值
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);		
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRegister()响应数据准备完毕，开始发送");  
    
    //发送数据包
    msgSend(p_sendbuf);
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleRegister()注册请求处理完毕，响应已发送，连接fd=%d", pConn->fd);  
  
    return true;
}
bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleLogIn()开始处理登录请求，连接fd=%d", pConn->fd);  
    
    if(pPkgBody == NULL)  
    {        
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::_HandleLogIn()包体为空，丢弃请求，连接fd=%d", pConn->fd);  
        return false;  
    }		    
    int iRecvLen = sizeof(STRUCT_LOGIN); 
    if(iRecvLen != iBodyLength) 
    {     
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::_HandleLogIn()包体长度不匹配，期望=%d，实际=%d，连接fd=%d",   
                         iRecvLen, iBodyLength, pConn->fd);  
        return false; 
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleLogIn()包体验证通过，开始处理登录数据");  
    
    CLock lock(&pConn->logicPorcMutex);
        
    LPSTRUCT_LOGIN p_RecvInfo = (LPSTRUCT_LOGIN)pPkgBody;     
    p_RecvInfo->username[sizeof(p_RecvInfo->username)-1]=0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password)-1]=0;
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleLogIn()收到登录请求，用户名=%s，连接fd=%d",   
                     p_RecvInfo->username, pConn->fd);  

	LPCOMM_PKG_HEADER pPkgHeader;	
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();

    int iSendLen = sizeof(STRUCT_LOGIN);
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleLogIn()准备响应数据，包体长度=%d", iSendLen);  
      
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader+iSendLen,false);    
    memcpy(p_sendbuf,pMsgHeader,m_iLenMsgHeader);    
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf+m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);    
    LPSTRUCT_LOGIN p_sendInfo = (LPSTRUCT_LOGIN)(p_sendbuf+m_iLenMsgHeader+m_iLenPkgHeader);
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);		   
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleLogIn()响应数据准备完毕，开始发送");  
    msgSend(p_sendbuf);  
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleLogIn()登录请求处理完毕，响应已发送，连接fd=%d", pConn->fd);  
    return true;
}

bool CLogicSocket::_HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandlePing()开始处理心跳包，连接fd=%d", pConn->fd);  
    if(iBodyLength != 0)  //有包体则认为是非法包  
    {  
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::_HandlePing()心跳包不应该有包体，丢弃请求，连接fd=%d", pConn->fd);  
        return false;   
    } 
    CLock lock(&pConn->logicPorcMutex); 
    time_t oldTime = pConn->lastPingTime;
    pConn->lastPingTime = time(NULL);   //更新变量
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandlePing()更新心跳时间，旧时间=%d，新时间=%d，连接fd=%d",   
                     oldTime, pConn->lastPingTime, pConn->fd);  
    //服务器也发送一个只有包头的数据包给客户端，作为返回的数据  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandlePing()准备发送心跳包响应");  
    SendNoBodyPkgToClient(pMsgHeader, _CMD_PING);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandlePing()心跳包处理完毕，响应已发送，连接fd=%d", pConn->fd);  
    return true;
}

//bool CLogicSocket::_HandleAddBook(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
//{
//    // 解析包体，假设包体包含书籍标题和作者
//    std::string title(pPkgBody, pPkgBody + 50);  // 假设标题长度为50
//    std::string author(pPkgBody + 50, pPkgBody + 100);  // 假设作者长度为50
//
//    m_libraryManager.AddBook(title, author);
//
//    // 发送响应给客户端
//    SendNoBodyPkgToClient(pMsgHeader, _CMD_ADD_BOOK);
//    return true;
//}

bool CLogicSocket::_HandleRemoveBook(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRemoveBook()开始处理移除书籍请求，连接fd=%d", pConn->fd);  
    // 解析包体，包体包含书籍ID
    int book_id = *reinterpret_cast<int*>(pPkgBody);
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleRemoveBook()收到移除书籍请求，书籍ID=%d，连接fd=%d",   
                     book_id, pConn->fd);
    m_libraryManager.RemoveBook(book_id);
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleRemoveBook()移除，书籍ID=%d",   
                      book_id);  

    // 发送响应给客户端  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRemoveBook()准备发送响应");  
    SendNoBodyPkgToClient(pMsgHeader, _CMD_REMOVE_BOOK);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleRemoveBook()处理完毕，响应已发送，连接fd=%d", pConn->fd);  
    return true;  
}

bool CLogicSocket::_HandleCheckOutBook(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleCheckOutBook()开始处理借出书籍请求，连接fd=%d", pConn->fd);  
    
    // 解析包体，包体包含书籍ID
    int book_id = *reinterpret_cast<int*>(pPkgBody);
    m_libraryManager.CheckOutBook(book_id);
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleCheckOutBook()收到借出书籍请求，书籍ID=%d，连接fd=%d",   
                     book_id, pConn->fd);  
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleCheckOutBook()借出书籍ID=%d"  
                    , book_id);  

    // 发送响应给客户端  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleCheckOutBook()准备发送响应");  
    SendNoBodyPkgToClient(pMsgHeader, _CMD_CHECKOUT_BOOK);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleCheckOutBook()处理完毕，响应已发送，连接fd=%d", pConn->fd);  
    return true;  
}

bool CLogicSocket::_HandleReturnBook(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleReturnBook()开始处理归还书籍请求，连接fd=%d", pConn->fd);  
    
    // 解析包体，包体包含书籍ID
    int book_id = *reinterpret_cast<int*>(pPkgBody);
    m_libraryManager.ReturnBook(book_id);
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleReturnBook()收到归还书籍请求，书籍ID=%d，连接fd=%d",   
                     book_id, pConn->fd);    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleReturnBook()归还书籍ID=%d",   
                      book_id);  

    // 发送响应给客户端  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleReturnBook()准备发送响应");  
    SendNoBodyPkgToClient(pMsgHeader, _CMD_RETURN_BOOK);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleReturnBook()处理完毕，响应已发送，连接fd=%d", pConn->fd);  
    return true; 
}

bool CLogicSocket::_HandleListBooks(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleListBooks()开始处理列出书籍请求，连接fd=%d", pConn->fd);  
    
    // 根据需要验证包体
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleListBooks()收到列出书籍请求，连接fd=%d", pConn->fd);  
    m_libraryManager.ListBooks();
    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleListBooks()成功列出本书籍");  
    // 发送响应给客户端  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleListBooks()准备发送响应");  
    SendNoBodyPkgToClient(pMsgHeader, _CMD_LIST_BOOKS);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleListBooks()处理完毕，响应已发送，连接fd=%d", pConn->fd);  
    return true;  

 }

bool CLogicSocket::_HandleAddBook(lpngx_connection_t pConn,
    LPSTRUC_MSG_HEADER pMsgHeader,
    char* pPkgBody,
    unsigned short iBodyLength)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleAddBook()开始处理添加书籍请求，连接fd=%d", pConn->fd);  
    
    // 检查包体合法性（假设至少需要标题50字节 + 作者50字节）
    if (pPkgBody == NULL || iBodyLength < 100)
    {
        ngx_log_error_core(NGX_LOG_WARN, 0, "CLogicSocket::_HandleAddBook()包体为空或长度不足，包体长度=%d，需要至少100字节，连接fd=%d",   
                         iBodyLength, pConn->fd);  
        
        // 这里可以选择返回错误反馈  
        const char* feedback = "Invalid packet body";  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleAddBook()发送错误响应：%s", feedback);  
        
        SendPkgWithBodyToClient(pMsgHeader, _CMD_ADD_BOOK, feedback, strlen(feedback));  
        return false; 
    }

    // 解析包体  
    std::string title(pPkgBody, pPkgBody + 50);   // 提取标题（50字节）  
    std::string author(pPkgBody + 50, pPkgBody + 100); // 提取作者（50字节）  

    // 为安全起见，可以确保字符串在最后一位结束符（根据实际需求）  
    title[49] = '\0';
    author[49] = '\0';

    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleAddBook()收到添加书籍请求，标题=%s，作者=%s，连接fd=%d",   
                     title.c_str(), author.c_str(), pConn->fd); 

    // 调用业务逻辑，添加书籍
    bool addResult = m_libraryManager.AddBook(title, author);

    ngx_log_error_core(NGX_LOG_INFO, 0, "CLogicSocket::_HandleAddBook()添加书籍操作%s，标题=%s",   
                     addResult ? "成功" : "失败", title.c_str());

    // 构造反馈消息，根据操作是否成功返回不同信息  
    std::string feedback;
    if (addResult)
    {
        feedback = "Add book success";
    }
    else
    {
        feedback = "Add book failure";
    }

    // 发送带有反馈消息的响应给客户端  
    SendPkgWithBodyToClient(pMsgHeader, _CMD_ADD_BOOK, feedback.c_str(), feedback.size());
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CLogicSocket::_HandleAddBook()处理完毕，响应已发送，连接fd=%d", pConn->fd);  

    return true;
}
