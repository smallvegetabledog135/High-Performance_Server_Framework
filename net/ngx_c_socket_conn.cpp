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

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//连接池成员函数
ngx_connection_s::ngx_connection_s()//构造函数
{		
    iCurrsequence = 0;    
    pthread_mutex_init(&logicPorcMutex, NULL); //互斥量初始化
}
ngx_connection_s::~ngx_connection_s()//析构函数
{
    pthread_mutex_destroy(&logicPorcMutex);    //互斥量释放
}

void ngx_connection_s::GetOneToUse()
{
    ++iCurrsequence;

    fd  = -1;                                         //开始先给-1
    curStat = _PKG_HD_INIT;                           //收包状态处于初始状态，准备接收数据包头【状态机】
    precvbuf = dataHeadInfo;                          //收包先收到这里，因为要先收包头，所以收数据的buff就是dataHeadInfo
    irecvlen = sizeof(COMM_PKG_HEADER);               //指定收数据的长度，这里要求收包头这么长字节的数据
    
    precvMemPointer   = NULL;                         
    iThrowsendCount   = 0;                            //原子的
    psendMemPointer   = NULL;                         //发送数据头指针记录
    events            = 0;                            //epoll事件先给0 
    lastPingTime      = time(NULL);                   //上次ping的时间

    FloodkickLastTime = 0;                            //Flood攻击上次收到包的时间
	FloodAttackCount  = 0;	                          //Flood攻击在该时间内收到包的次数统计
    iSendCount        = 0;                            //发送队列中有的数据条目数，若client只发不收，则可能造成此数过大，依据此数做出踢出处理 
}

void ngx_connection_s::PutOneToFree()
{
    ++iCurrsequence;   
    if(precvMemPointer != NULL)//给这个连接分配过接收数据的内存，则要释放内存
    {        
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = NULL;        
    }
    if(psendMemPointer != NULL) //如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = NULL;
    }

    iThrowsendCount = 0;            
}

//初始化连接池
void CSocekt::initconnection()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始初始化连接池...");
    lpngx_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();   

    int ilenconnpool = sizeof(ngx_connection_t);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "连接对象大小: %d 字节", ilenconnpool);  
    for(int i = 0; i < m_worker_connections; ++i)
    {
        p_Conn = (lpngx_connection_t)p_memory->AllocMemory(ilenconnpool,true); //清理内存
        p_Conn = new(p_Conn) ngx_connection_t();  		
        p_Conn->GetOneToUse();
        m_connectionList.push_back(p_Conn);     //所有链接都放在这个list
        m_freeconnectionList.push_back(p_Conn); //空闲连接会放在这个list
    } 
    m_free_connection_n = m_total_connection_n = m_connectionList.size(); 
    ngx_log_error_core(NGX_LOG_INFO, 0, "连接池初始化完成，共分配 %d 个连接", m_total_connection_n.load());  
    return;
}

//回收连接池，释放内存
void CSocekt::clearconnection()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始清理连接池...");
    lpngx_connection_t p_Conn;
	CMemory *p_memory = CMemory::GetInstance();
    int freed_count = 0; 
	
	while(!m_connectionList.empty())
	{
		p_Conn = m_connectionList.front();
		m_connectionList.pop_front(); 
        p_Conn->~ngx_connection_t();     
		p_memory->FreeMemory(p_Conn);
        freed_count++;
        if(freed_count % 1000 == 0)  
        {  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "已释放 %d 个连接对象...", freed_count);  
        } 
	}
    ngx_log_error_core(NGX_LOG_INFO, 0, "连接池清理完成，共释放 %d 个连接", freed_count);
}

//从连接池中获取一个空闲连接
lpngx_connection_t CSocekt::ngx_get_connection(int isock)
{
    CLock lock(&m_connectionMutex);  

    if(!m_freeconnectionList.empty())
    {
        lpngx_connection_t p_Conn = m_freeconnectionList.front(); //返回第一个元素但不检查元素存在与否
        m_freeconnectionList.pop_front();                         //移除第一个元素但不返回	
        p_Conn->GetOneToUse();
        --m_free_connection_n; 
        p_Conn->fd = isock;
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "从连接池获取空闲连接成功，fd=%d，剩余空闲连接: %d",   
                          isock, m_free_connection_n.load());
        return p_Conn;
    }

    // 没有空闲连接，创建新连接  
    ngx_log_error_core(NGX_LOG_INFO, 0, "连接池中无空闲连接，创建新的连接对象，fd=%d", isock);  
    

    //没空闲的连接，重新创建一个连接
    CMemory *p_memory = CMemory::GetInstance();
    lpngx_connection_t p_Conn = (lpngx_connection_t)p_memory->AllocMemory(sizeof(ngx_connection_t),true);
    p_Conn = new(p_Conn) ngx_connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn); 
    ++m_total_connection_n;             
    p_Conn->fd = isock;

    ngx_log_error_core(NGX_LOG_INFO, 0, "创建新连接成功，当前连接总数: %d，fd=%d",   
                      m_total_connection_n.load(), isock); 
    return p_Conn;

}

//归还参数pConn所代表的连接到到连接池中
void CSocekt::ngx_free_connection(lpngx_connection_t pConn) 
{
    CLock lock(&m_connectionMutex);   
    int fd = pConn->fd;

    //所有连接全部都在m_connectionList里；
    pConn->PutOneToFree();
    //扔到空闲连接列表里
    m_freeconnectionList.push_back(pConn);
    //空闲连接数+1
    ++m_free_connection_n;
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "连接已归还到连接池，fd=%d，当前空闲连接数: %d",   
                      fd, m_free_connection_n.load());
    return;
}


//将要回收的连接放到一个队列中来，后续有专门的线程处理这个队列中的连接的回收
void CSocekt::inRecyConnectQueue(lpngx_connection_t pConn)
{
    int fd = pConn->fd;
    std::list<lpngx_connection_t>::iterator pos;
    bool iffind = false;
        
    CLock lock(&m_recyconnqueueMutex); //针对连接回收列表的互斥量

    for(pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); ++pos)
	{
		if((*pos) == pConn)		
		{	
			iffind = true;
			break;			
		}
	}
    if(iffind == true)
	{
		ngx_log_error_core(NGX_LOG_WARN, 0, "连接已在回收队列中，忽略重复添加，fd=%d", fd);  
        return; 
    }

    pConn->inRecyTime = time(NULL);        //记录回收时间
    ++pConn->iCurrsequence;
    m_recyconnectionList.push_back(pConn); //等待ServerRecyConnectionThread线程自会处理 
    ++m_totol_recyconnection_n;            //待释放连接队列大小+1
    --m_onlineUserCount;                   //连入用户数量-1
    ngx_log_error_core(NGX_LOG_INFO, 0, "连接加入回收队列，fd=%d，当前回收队列大小: %d，在线用户数: %d",   
                      fd, m_totol_recyconnection_n.load(), m_onlineUserCount.load());  
    return; 
}

//处理连接回收的线程
void* CSocekt::ServerRecyConnectionThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocekt *pSocketObj = pThread->_pThis;
    
    time_t currtime;
    int err;
    std::list<lpngx_connection_t>::iterator pos,posend;
    lpngx_connection_t p_Conn;

    ngx_log_error_core(NGX_LOG_INFO, 0, "连接回收线程已启动");
    
    while(1)
    {
        usleep(200 * 1000); 

        if(pSocketObj->m_totol_recyconnection_n > 0)
        {
            currtime = time(NULL);
            err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);  
            if(err != 0)  
            {  
                ngx_log_error_core(NGX_LOG_ALERT, err, "连接回收线程pthread_mutex_lock()失败，错误码: %d", err);  
            }
            int processedCount = 0;

lblRRTD:
            pos    = pSocketObj->m_recyconnectionList.begin();
			posend = pSocketObj->m_recyconnectionList.end();
            for(; pos != posend; ++pos)
            {
                p_Conn = (*pos);
                if(
                    ( (p_Conn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime)  && (g_stopEvent == 0) //如果不是要整个系统退出，你可以continue，否则就得要强制释放
                    )
                {
                    continue; 
                }    

                if(p_Conn->iThrowsendCount > 0)
                {
                    ngx_log_error_core(NGX_LOG_WARN, 0, "连接到达释放时间但仍有未发送完的数据，fd=%d，未发送数据包数: %d",   
                                      p_Conn->fd, p_Conn->iThrowsendCount.load());
                }

                //开始释放
                --pSocketObj->m_totol_recyconnection_n;        //待释放连接队列大小-1
                pSocketObj->m_recyconnectionList.erase(pos);  

                ngx_log_error_core(NGX_LOG_DEBUG, 0, "连接已从回收队列释放，fd=%d，当前回收队列大小: %d",   
                                  p_Conn->fd, pSocketObj->m_totol_recyconnection_n.load());

                pSocketObj->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
                processedCount++;
                goto lblRRTD; 
            } 
            err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex); 
            if(err != 0)  ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        } 

        if(g_stopEvent == 1)
        {
            if(pSocketObj->m_totol_recyconnection_n > 0)
            {
                ngx_log_error_core(NGX_LOG_WARN, 0, "程序退出，强制释放回收队列中的所有连接，数量: %d",   
                                  pSocketObj->m_totol_recyconnection_n.load());
                err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);  
                if(err != 0)  
                {  
                    ngx_log_error_core(NGX_LOG_ALERT, err, "连接回收线程pthread_mutex_lock2()失败，错误码: %d", err);  
                } 
                int forcedReleaseCount = 0;
        lblRRTD2:
                pos    = pSocketObj->m_recyconnectionList.begin();
			    posend = pSocketObj->m_recyconnectionList.end();
                for(; pos != posend; ++pos)
                {
                    p_Conn = (*pos);
                    --pSocketObj->m_totol_recyconnection_n;        //待释放连接队列大小-1
                    pSocketObj->m_recyconnectionList.erase(pos); 
 
                    ngx_log_error_core(NGX_LOG_DEBUG, 0, "强制释放连接，fd=%d", p_Conn->fd);  
                    
                    pSocketObj->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
                    goto lblRRTD2; 
                } 
                err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex); 
                if(err != 0)  
                {  
                    ngx_log_error_core(NGX_LOG_ALERT, err, "连接回收线程pthread_mutex_unlock2()失败，错误码: %d", err);  
                }  
                
                ngx_log_error_core(NGX_LOG_INFO, 0, "强制释放完成，共释放 %d 个连接", forcedReleaseCount);  
            }
            ngx_log_error_core(NGX_LOG_INFO, 0, "连接回收线程退出");  
            break;
        }  
    }  
    
    return (void*)0;
}

void CSocekt::ngx_close_connection(lpngx_connection_t pConn)
{    
    ngx_free_connection(pConn); 
    if(pConn->fd != -1)
    {
        close(pConn->fd);
        pConn->fd = -1;
    }    
    return;
}
