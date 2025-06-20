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
#include <sys/ioctl.h> 
#include <arpa/inet.h>
#include <pthread.h>   

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"  

//当连接上有数据来的时候，本函数被ngx_epoll_process_events()所调用
void CSocekt::ngx_read_request_handler(lpngx_connection_t pConn)
{   
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "开始处理客户端数据，连接fd=%d", pConn->fd);  
    bool isflood = false; //是否flood攻击；
// 预设的数据包  
    const unsigned char presetPacket[20+8 + 50] = {  
        // 消息头（20 字节，填充为0）  
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
        // 包头（假设当前语义和数据格式）  
        0x03, 0xE9, // msgCode: 1001 (CMD_ADD_BOOK)  
        0x00, 0x16, // pkgLen: 包头 + 包体（8 + 30）  
        0x00, 0x00, 0x00, 0x00, // crc32: 0  
        // 包体（书籍标题和作者，'\0' 作为分隔符）  
        'T', 'h', 'e', ' ', 'G', 'r', 'e', 'a', 't', ' ', 'G', 'a', 't', 's', 'b', 'y', '\0',  
        'F', '.', ' ', 'S', 'c', 'o', 't', 't', ' ', 'F', 'i', 't', 'z', 'g', 'e', 'r', 'a', 'l', 'd', '\0'  
    };  

    // 将预设数据包直接放入连接的接收缓冲区  
    memcpy(pConn->precvbuf, presetPacket, sizeof(presetPacket));  
    pConn->irecvlen = sizeof(presetPacket); // 更新接收长度

    //收包
    ssize_t reco = recvproc(pConn,pConn->precvbuf,pConn->irecvlen); 
    if(reco <= 0)  
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "接收数据失败或无数据可接收，fd=%d，返回值=%d", pConn->fd, reco);  
        return;        
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "接收到数据，连接fd=%d，接收长度=%d字节", pConn->fd, reco);  
       
    for (ssize_t i = 0; i < reco; i++) {  
        ngx_log_stderr(0, "%02x", static_cast<unsigned char>(pConn->precvbuf[i]));   
ngx_log_stderr(0, "\n"); 
 }  
    //判断收到了多少数据     
    if(pConn->curStat == _PKG_HD_INIT) 
    {         
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "当前连接状态: 准备接收包头, fd=%d", pConn->fd);  
        
        if(reco == m_iLenPkgHeader)//收到完整包头，这里拆解包头
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "接收到完整包头，开始解析包头, fd=%d", pConn->fd);  
            ngx_wait_request_handler_proc_p1(pConn, isflood); // 调用专门针对包头处理完整的函数处理  
        }
        else
		{
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包头不完整，需要继续接收, fd=%d, 已接收=%d, 包头长度=%d",   
                              pConn->fd, reco, m_iLenPkgHeader);  
            
			//收到的包头不完整
            pConn->curStat        = _PKG_HD_RECVING;                 //接收包头中，包头不完整，继续接收包头中	
            pConn->precvbuf       = pConn->precvbuf + reco;              //收后续包的内存往后走
            pConn->irecvlen       = pConn->irecvlen - reco;             
        } 
    } 
    else if(pConn->curStat == _PKG_HD_RECVING) //接收包头中，包头不完整，继续接收中
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "继续接收包头数据, fd=%d, 本次接收=%d, 待接收=%d",   
                          pConn->fd, reco, pConn->irecvlen);  
        if(pConn->irecvlen == reco) //要求收到的宽度和实际收到的宽度相等
        {
            //包头收完整
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包头接收完整，开始解析, fd=%d", pConn->fd);  
            ngx_wait_request_handler_proc_p1(pConn, isflood); // 调用专门针对包头处理完整的函数处理  
        }
        else
		{
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包头继续接收中, fd=%d, 已接收=%d, 剩余需接收=%d",   
                              pConn->fd, reco, pConn->irecvlen - reco);  
            
            pConn->precvbuf       = pConn->precvbuf + reco;              //收后续包的内存往后走
            pConn->irecvlen       = pConn->irecvlen - reco;              
        }
    }
    else if(pConn->curStat == _PKG_BD_INIT) 
    {
        //包头刚好收完，准备接收包体
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "准备接收包体, fd=%d, 包体长度=%d", pConn->fd, pConn->irecvlen);  
        
        if(reco == pConn->irecvlen)
        {
            //收到的宽度等于要收的宽度，包体也收完整
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包体接收完整, fd=%d", pConn->fd);  
            
            if(m_floodAkEnable == 1) 
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
                if(isflood)  
                {  
                    ngx_log_error_core(NGX_LOG_WARN, 0, "检测到可能的Flood攻击, fd=%d", pConn->fd);  
                } 
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        }
        else
		{
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包体接收不完整，继续接收, fd=%d, 已接收=%d, 包体长度=%d",   
                              pConn->fd, reco, pConn->irecvlen);  
            
			//收到的宽度小于要收的宽度
			pConn->curStat = _PKG_BD_RECVING;					
			pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
		}
    }
    else if(pConn->curStat == _PKG_BD_RECVING) 
    {
        //接收包体中，包体不完整，继续接收中
         ngx_log_error_core(NGX_LOG_DEBUG, 0, "继续接收包体, fd=%d, 本次接收=%d, 剩余需接收=%d",   
                          pConn->fd, reco, pConn->irecvlen);  
       
        if(pConn->irecvlen == reco)
        {
            //包体收完整
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包体接收完整, fd=%d", pConn->fd);  
            
            if(m_floodAkEnable == 1) 
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
                if(isflood)  
                {  
                    ngx_log_error_core(NGX_LOG_WARN, 0, "检测到可能的Flood攻击, fd=%d", pConn->fd);  
                }
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        }
        else
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "包体继续接收中, fd=%d, 已接收=%d, 剩余需接收=%d",   
                              pConn->fd, reco, pConn->irecvlen - reco);  
            
            //包体没收完整，继续收
            pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
        }
    }  

    if(isflood == true)
    {
        //客户端flood服务器，则直接把客户端踢掉
        ngx_log_error_core(NGX_LOG_WARN, 0, "客户端可能发起Flood攻击，关闭连接, fd=%d", pConn->fd);
        zdClosesocketProc(pConn);
    }
    return;
}

ssize_t CSocekt::recvproc(lpngx_connection_t pConn,char *buff,ssize_t buflen)  //ssize_t是有符号整型，在32位机器上等同与int，在64位机器上等同与long int，size_t就是无符号型的ssize_t
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "准备接收数据, fd=%d, 缓冲区大小=%d", pConn->fd, buflen);  
    
    ssize_t n;
    
    n = recv(pConn->fd, buff, buflen, 0);     
    if(n == 0)
    {
        ngx_log_error_core(NGX_LOG_INFO, 0, "客户端已关闭连接, fd=%d", pConn->fd);  
        zdClosesocketProc(pConn);        
        return -1; 
    }
    if(n < 0) //有错误发生
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            ngx_log_error_core(NGX_LOG_DEBUG, errno, "recvproc()中errno为EAGAIN或EWOULDBLOCK, fd=%d", pConn->fd);  
            return -1;
        }
        if(errno == EINTR)  
        {
            ngx_log_error_core(NGX_LOG_DEBUG, errno, "recvproc()中errno为EINTR, fd=%d", pConn->fd);  
            return -1; 
        }

        ngx_log_error_core(NGX_LOG_ERR, errno, "recvproc()接收数据出错, fd=%d", pConn->fd);  
        zdClosesocketProc(pConn);  
        return -1; 
    }

    //收到了有效数据
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "成功接收数据, fd=%d, 接收长度=%d", pConn->fd, n);  
    return n; // 返回收到的字节数
}


//包处理阶段
void CSocekt::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn,bool &isflood)
{    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()开始处理包头, 连接fd=%d", pConn->fd);  


    CMemory *p_memory = CMemory::GetInstance();		

    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; //正好收到包头时，包头信息在dataHeadInfo里；

    unsigned short e_pkgLen; 
    e_pkgLen = ntohs(pPkgHeader->pkgLen);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()包头解析: pkgLen=%d, 连接fd=%d", e_pkgLen, pConn->fd);  
    
    //恶意包或者错误包的判断
    if(e_pkgLen < m_iLenPkgHeader) 
    {
        ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ngx_wait_request_handler_proc_p1()包长度异常，小于包头长度，e_pkgLen=%d，连接fd=%d", e_pkgLen, pConn->fd);  
        
        pConn->curStat = _PKG_HD_INIT;      
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else if(e_pkgLen > (_PKG_MAX_LENGTH-1000))
    {
        ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ngx_wait_request_handler_proc_p1()包长度异常，超过最大限制，e_pkgLen=%d，连接fd=%d", e_pkgLen, pConn->fd);  
        
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()包头合法，准备处理包体，连接fd=%d", pConn->fd);  


        //合法的包头，继续处理
        char *pTmpBuffer  = (char *)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen,false); //分配内存【长度是 消息头长度  + 包头长度 + 包体长度】       
        pConn->precvMemPointer = pTmpBuffer;  //内存开始指针
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()已分配内存，大小=%d，连接fd=%d", m_iLenMsgHeader + e_pkgLen, pConn->fd);  

        //填写消息头内容
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = pConn;
        ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence; //收到包时的连接池中连接序号记录到消息头；
        //填写包头内容
        pTmpBuffer += m_iLenMsgHeader;                 //跳过消息头，指向包头
        memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader); //把收到的包头拷贝进来
        if(e_pkgLen == m_iLenPkgHeader)
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()包长度等于包头长度，无包体，可以直接处理，连接fd=%d", pConn->fd);  


            //入消息队列待后续业务逻辑线程处理
            if(m_floodAkEnable == 1) 
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
                if(isflood) {  
                    ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ngx_wait_request_handler_proc_p1()检测到Flood攻击行为，连接fd=%d", pConn->fd);  
                } 
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        } 
        else
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()包长度大于包头长度，需要接收包体，包体长度=%d，连接fd=%d",   
                             e_pkgLen - m_iLenPkgHeader, pConn->fd);  


            pConn->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;  //pTmpBuffer指向包头
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小\m_iLenPkgHeader【包头】\包体
        }                       
    }  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_p1()处理完毕，包长度=%d，包头长度=%d，连接fd=%d",   
                     e_pkgLen, m_iLenPkgHeader, pConn->fd);  
    return;
}

void CSocekt::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn,bool &isflood)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_plast()开始处理完整包，连接fd=%d", pConn->fd);  


    if(isflood == false)  
    {  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_plast()将消息放入接收队列，连接fd=%d", pConn->fd);  
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer); //入消息队列并触发线程处理消息  
    } 
    else
    {
        ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ngx_wait_request_handler_proc_plast()检测到攻击倾向，丢弃消息，连接fd=%d", pConn->fd);  
        CMemory *p_memory = CMemory::GetInstance();
        p_memory->FreeMemory(pConn->precvMemPointer); //直接释放掉内存，不往消息队列入
    }

    pConn->precvMemPointer = NULL;
    pConn->curStat         = _PKG_HD_INIT;     //收包状态机的状态恢复为原始态，为收下一个包做准备                    
    pConn->precvbuf        = pConn->dataHeadInfo;  //设置好收包的位置
    pConn->irecvlen        = m_iLenPkgHeader;  //设置好要接收数据的大小
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_wait_request_handler_proc_plast()重置连接状态完成，准备接收下一个包，连接fd=%d", pConn->fd);  
    return;
}

//发送数据函数，返回本次发送的字节数
//返回>0，成功发送了一些字节
//=0，对方断KAI
//-1，errno == EAGAIN ，本方发送缓冲区满了
//-2，errno != EAGAIN != EWOULDBLOCK != EINTR
ssize_t CSocekt::sendproc(lpngx_connection_t c,char *buff,ssize_t size)   
{
    ssize_t   n;
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::sendproc()开始发送数据，连接fd=%d，数据长度=%d", c->fd, size);  

    for ( ;; )
    {
        n = send(c->fd, buff, size, 0);  
        if(n > 0) //成功发送了一些数据  
        {        
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::sendproc()成功发送数据，连接fd=%d，发送长度=%d", c->fd, n);  
            return n; //返回本次发送的字节数  
        } 

        if(n == 0)  
        {  
            ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::sendproc()发送0字节，连接可能已断开，fd=%d", c->fd);  
            return 0;  
        } 

        if(errno == EAGAIN)  
        {  
            //内核缓冲区满 
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::sendproc()发送缓冲区已满，连接fd=%d", c->fd);  
            return -1;  //发送缓冲区满  
        }  

        if(errno == EINTR)   
        {  
            ngx_log_stderr(errno, "CSocekt::sendproc()中send()失败.");  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::sendproc()send()失败，连接fd=%d", c->fd);           
        }  
        else  
        {   
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::sendproc()send()发生错误，连接fd=%d", c->fd);  
            return -2;    
        }  
    } 
}

//设置数据发送时的写处理函数,当数据可写时epoll通知我们
//数据就是没法送完毕，要继续发送
void CSocekt::ngx_write_request_handler(lpngx_connection_t pConn)
{      
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_write_request_handler()开始处理写事件，连接fd=%d", pConn->fd);  
    CMemory *p_memory = CMemory::GetInstance();
    ssize_t sendsize = sendproc(pConn,pConn->psendbuf,pConn->isendlen);

    if(sendsize > 0 && sendsize != pConn->isendlen)
    {        
        //没有全部发送完毕，数据只发出去了一部分
        pConn->psendbuf = pConn->psendbuf + sendsize;
		pConn->isendlen = pConn->isendlen - sendsize;	
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_write_request_handler()数据部分发送，连接fd=%d，已发送=%d，剩余=%d",   
                         pConn->fd, sendsize, pConn->isendlen);  
        return;
    }
    else if(sendsize == -1)
    {
        ngx_log_stderr(errno, "CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。"); 
        ngx_log_error_core(NGX_LOG_WARN, errno, "CSocekt::ngx_write_request_handler()发送缓冲区满，但epoll通知可写，连接fd=%d", pConn->fd);  
        return;
    }

    if(sendsize > 0 && sendsize == pConn->isendlen) 
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_write_request_handler()数据发送完毕，连接fd=%d，发送长度=%d", pConn->fd, sendsize);  
        if(ngx_epoll_oper_event(
                pConn->fd,          //socket句柄
                EPOLL_CTL_MOD,      //事件类型
                EPOLLOUT,           //标志，这里代表要减去的标志,EPOLLOUT：可写【可写的时候通知我】
                1,                  //对于事件类型为增加的， 0：增加   1：去掉 2：完全覆盖
                pConn               //连接池中的连接
                ) == -1)
        {
            ngx_log_stderr(errno, "CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_write_request_handler()从epoll中移除写事件失败，连接fd=%d", pConn->fd);  
        }    
    }

    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_write_request_handler()执行发送完毕收尾工作，连接fd=%d", pConn->fd);  

    if(sem_post(&m_semEventSendQueue) == -1)  
    {  
        ngx_log_stderr(0, "CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_write_request_handler()发送信号量失败，连接fd=%d", pConn->fd);  
    } 

    p_memory->FreeMemory(pConn->psendMemPointer);  //释放内存
    pConn->psendMemPointer = NULL;        
    --pConn->iThrowsendCount;  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_write_request_handler()处理完毕，连接fd=%d，当前iThrowsendCount=%d",   
                     pConn->fd, pConn->iThrowsendCount.load());  
    return; 
}

//消息本身格式【消息头+包头+包体】 
void CSocekt::threadRecvProcFunc(char *pMsgBuf)
{   
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::threadRecvProcFunc()开始处理接收到的消息");  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::threadRecvProcFunc()消息处理完毕");  
    return;  
}


