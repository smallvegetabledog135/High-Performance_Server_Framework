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

//设置踢出时钟
void CSocekt::AddToTimerQueue(lpngx_connection_t pConn)
{
	ngx_log_error_core(NGX_LOG_DEBUG, 0, "开始添加连接到超时检查队列, fd=%d", pConn->fd);  
    
    CMemory *p_memory = CMemory::GetInstance();

    time_t futtime = time(NULL);
    futtime += m_iWaitTime; 

	ngx_log_error_core(NGX_LOG_DEBUG, 0, "连接将在 %d 秒后超时检查, fd=%d, 超时时间点=%ui",   
                      m_iWaitTime, pConn->fd, (unsigned int)futtime);  


    CLock lock(&m_timequeueMutex); //互斥
    LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader,false);
    tmpMsgHeader->pConn = pConn;
    tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
    m_timerQueuemap.insert(std::make_pair(futtime,tmpMsgHeader)); //按键自动排序小->大
    m_cur_size_++;  //计时队列+1
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "连接已添加到超时检查队列, fd=%d, 当前队列大小=%d",   
                      pConn->fd, m_cur_size_);  
    
    m_timer_value_ = GetEarliestTime(); // 计时队列头部时间值保存到m_timer_value_里  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "更新最早超时时间点为 %ui", (unsigned int)m_timer_value_);  
    
    return;    
}

time_t CSocekt::GetEarliestTime()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;	
	pos = m_timerQueuemap.begin();		
	ngx_log_error_core(NGX_LOG_DEBUG, 0, "获取最早超时时间点: %ui", (unsigned int)pos->first);  
    return pos->first;	
}

//从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针返回
LPSTRUC_MSG_HEADER CSocekt::RemoveFirstTimer()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;	
	LPSTRUC_MSG_HEADER p_tmp;
	if(m_cur_size_ <= 0)
	{
		ngx_log_error_core(NGX_LOG_DEBUG, 0, "RemoveFirstTimer()被调用但计时队列为空");  
        return NULL;
	}
	pos = m_timerQueuemap.begin(); 
	p_tmp = pos->second;
	m_timerQueuemap.erase(pos);
	--m_cur_size_;
	ngx_log_error_core(NGX_LOG_DEBUG, 0, "移除后计时队列大小=%d", m_cur_size_);  
    return p_tmp;
}

LPSTRUC_MSG_HEADER CSocekt::GetOverTimeTimer(time_t cur_time)
{	
	CMemory *p_memory = CMemory::GetInstance();
	LPSTRUC_MSG_HEADER ptmp;

	if (m_cur_size_ == 0 || m_timerQueuemap.empty())
		return NULL; //队列为空

	time_t earliesttime = GetEarliestTime(); //到multimap中去查询

	ngx_log_error_core(NGX_LOG_DEBUG, 0, "检查超时: 当前时间=%ui, 最早超时时间=%ui",   
                      (unsigned int)cur_time, (unsigned int)earliesttime);  
     
	if (earliesttime <= cur_time)
	{
		//这回确实是有【超时的节点】
		ptmp = RemoveFirstTimer();    //把这个超时的节点从 m_timerQueuemap 删掉，并把这个节点的第二项返回来；

		if(/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick != 1)
		{      
			time_t newinqueutime = cur_time+(m_iWaitTime);
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(sizeof(STRUC_MSG_HEADER),false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrsequence = ptmp->iCurrsequence;			
			m_timerQueuemap.insert(std::make_pair(newinqueutime,tmpMsgHeader)); //自动排序 小->大			
			m_cur_size_++; 

			ngx_log_error_core(NGX_LOG_DEBUG, 0, "连接超时但不踢出，重新加入队列, fd=%d, 新超时时间点=%ui",   
                              ptmp->pConn->fd, (unsigned int)newinqueutime);  
              
		}

		if(m_cur_size_ > 0) 
		{
			m_timer_value_ = GetEarliestTime(); //计时队列头部时间值保存到m_timer_value_里
		    ngx_log_error_core(NGX_LOG_DEBUG, 0, "更新最早超时时间点为 %ui", (unsigned int)m_timer_value_);  
        
		}
		return ptmp;
	}
	return NULL;
}

//把指定用户tcp连接从timer表中抠出去
void CSocekt::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
	ngx_log_error_core(NGX_LOG_DEBUG, 0, "开始从超时检查队列中删除连接, fd=%d", pConn->fd);  
    
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;
	CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_timequeueMutex);
lblMTQM:
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for(; pos != posend; ++pos)	
	{
		if(pos->second->pConn == pConn)
		{			
			ngx_log_error_core(NGX_LOG_DEBUG, 0, "从超时检查队列中找到并删除连接, fd=%d, 超时时间点=%ui",   
                              pConn->fd, (unsigned int)pos->first);  
            
			p_memory->FreeMemory(pos->second);  //释放内存
			m_timerQueuemap.erase(pos);
			--m_cur_size_; //减去一个元素;								
			goto lblMTQM;
		}		
	}
	if(m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "更新最早超时时间点为 %ui", (unsigned int)m_timer_value_);  
    }
    return;    
}

//清理时间队列中所有内容
void CSocekt::clearAllFromTimerQueue()
{	
	ngx_log_error_core(NGX_LOG_INFO, 0, "开始清理所有超时检查队列项, 当前队列大小=%d", m_cur_size_);  
    
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;

	CMemory *p_memory = CMemory::GetInstance();	
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end(); 
	int clearedCount = 0;   
	for(; pos != posend; ++pos)	
	{
		if(pos->second != NULL && pos->second->pConn != NULL)  
        {  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "清理超时检查队列项, fd=%d, 超时时间点=%ui",   
                              pos->second->pConn->fd, (unsigned int)pos->first);  
        }  
        
        p_memory->FreeMemory(pos->second);        
        --m_cur_size_;  
        clearedCount++; 		
	}
	m_timerQueuemap.clear();

	ngx_log_error_core(NGX_LOG_INFO, 0, "超时检查队列已清空, 共清理%d项", clearedCount);  
    return;
}

//时间队列监视和处理线程，处理到期不发心跳包的用户踢出的线程
void* CSocekt::ServerTimerQueueMonitorThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocekt *pSocketObj = pThread->_pThis;

    time_t absolute_time,cur_time;
    int err;
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ServerTimerQueueMonitorThread()超时检查线程已启动");  
    
    while(g_stopEvent == 0) //不退出
    {		
		if(pSocketObj->m_cur_size_ > 0)//队列不为空，有内容
        {
            absolute_time = pSocketObj->m_timer_value_; 
            cur_time = time(NULL);
			ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerTimerQueueMonitorThread()检查时间队列，当前队列大小=%d，最近超时时间=%d，当前时间=%d",   
                             pSocketObj->m_cur_size_, absolute_time, cur_time);  
            
            if(absolute_time < cur_time)
            {
				ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerTimerQueueMonitorThread()时间已到，开始处理超时事件");  
                
                std::list<LPSTRUC_MSG_HEADER> m_lsIdleList; 
                LPSTRUC_MSG_HEADER result;

                err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);  
                if(err != 0) {  
                    ngx_log_stderr(err, "CSocekt::ServerTimerQueueMonitorThread()中pthread_mutex_lock()失败，返回的错误码为%d!", err); //有问题，要及时报告  
                    ngx_log_error_core(NGX_LOG_ERR, err, "CSocekt::ServerTimerQueueMonitorThread()加锁互斥量失败");  
                } 
				while ((result = pSocketObj->GetOverTimeTimer(cur_time)) != NULL) 
				{
					m_lsIdleList.push_back(result); 
				}
				ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerTimerQueueMonitorThread()获取到%d个超时节点", m_lsIdleList.size());  
                
                err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex); 
                if(err != 0) {  
                    ngx_log_stderr(err, "CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err); //有问题，要及时报告  
                    ngx_log_error_core(NGX_LOG_ERR, err, "CSocekt::ServerTimerQueueMonitorThread()解锁互斥量失败");  
                } 
				LPSTRUC_MSG_HEADER tmpmsg;
				int processedCount = 0; 
                while(!m_lsIdleList.empty())
                {
                    tmpmsg = m_lsIdleList.front();
					m_lsIdleList.pop_front();

					ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerTimerQueueMonitorThread()处理第%d个超时节点，连接fd=%d",   
                                     ++processedCount, tmpmsg->pConn ? tmpmsg->pConn->fd : -1);  
                     
                    pSocketObj->procPingTimeOutChecking(tmpmsg,cur_time); //检查心跳超时问题
                } 
				ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerTimerQueueMonitorThread()已处理完所有超时节点，共%d个", processedCount);  
            
            }
        } 
        
        usleep(500 * 1000); //为简化问题，我们直接每次休息500毫秒
    } 

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ServerTimerQueueMonitorThread()超时检查线程已退出");  
    return (void*)0;
}

//心跳包检测时间到，检测心跳包是否超时
void CSocekt::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time)
{
	ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::procPingTimeOutChecking()处理心跳包超时检查，连接fd=%d，当前时间=%d",   
                     tmpmsg->pConn ? tmpmsg->pConn->fd : -1, cur_time);  

    CMemory *p_memory = CMemory::GetInstance();  
    p_memory->FreeMemory(tmpmsg);  
    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::procPingTimeOutChecking()释放了超时节点的内存");  
    
}


