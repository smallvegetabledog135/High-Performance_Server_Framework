#include <stdarg.h>
#include <unistd.h> 
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

//静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;  //#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;     //#define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_shutdown = false;      

//构造函数
CThreadPool::CThreadPool()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池对象创建"); 
    m_iRunningThreadNum = 0;  //正在运行的线程
    m_iLastEmgTime = 0;       
    //m_iPrintInfoTime = 0;  
    m_iRecvMsgQueueCount = 0; //收消息队列
}

//析构函数
CThreadPool::~CThreadPool()
{    
    //接收消息队列中内容释放
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池对象即将销毁");  
    clearMsgRecvQueue();  
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池消息队列已清空");
}

//清理函数
//清理接收消息队列
void CThreadPool::clearMsgRecvQueue()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始清理线程池消息接收队列");
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();
    int cleared_count = 0;

	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
        cleared_count++;
	}	
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池消息接收队列清理完毕，共清理 %d 条消息", cleared_count);
}

//创建线程池中的线程，要手工调用
//返回值：所有线程都创建成功则返回true，出现错误则返回false
bool CThreadPool::Create(int threadNum)
{    
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始创建线程池，计划创建 %d 个线程", threadNum);
    ThreadItem *pNew;
    int err;

    m_iThreadNum = threadNum; //保存要创建的线程数量    
    
    for(int i = 0; i < m_iThreadNum; ++i)
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "创建线程池中第 %d 个线程", i);
        m_threadVector.push_back(pNew = new ThreadItem(this));             //创建一个新线程对象并入到容器中         
        err = pthread_create(&pNew->_Handle, NULL, ThreadFunc, pNew);      //创建线程，错误不返回到errno，一般返回错误码
        if(err != 0)
        {
            ngx_log_error_core(NGX_LOG_ERR, 0, "线程池创建第 %d 个线程失败，错误码: %d", i, err);  
            return false;
        }
        else
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "线程池创建第 %d 个线程成功，线程ID: %lu", i, pNew->_Handle);  
        }        
    } 
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池中所有线程创建完毕，等待线程初始化..."); 

    //必须保证每个线程都启动并运行到pthread_cond_wait()，本函数才返回
    std::vector<ThreadItem*>::iterator iter;
lblfor:
    for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if( (*iter)->ifrunning == false) //保证所有线程完全启动起来，以保证整个线程池中的线程正常工作；
        {
            //说明有没有启动完全的线程
            usleep(100 * 1000);
            goto lblfor;
        }
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池创建成功，所有 %d 个线程已就绪", m_iThreadNum);  
    return true;
}

//线程入口函数，当用pthread_create()创建线程后，ThreadFunc()函数都会被立即执行；
void* CThreadPool::ThreadFunc(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool *pThreadPoolObj = pThread->_pThis;
    
    CMemory *p_memory = CMemory::GetInstance();	    
    int err;

    pthread_t tid = pthread_self();  
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程 %lu 已启动", tid);  
        
    while(true)
    {
        //线程用pthread_mutex_lock()函数去锁定指定的mutex变量，若该mutex已经被另外一个线程锁定了，该调用将会阻塞线程直到mutex被解锁。  
        err = pthread_mutex_lock(&m_pthreadMutex);  
        if(err != 0)  
        {  
            ngx_log_error_core(NGX_LOG_ERR, err, "线程 %lu 获取互斥锁失败，错误码: %d", tid, err);  
        } 

        while ( (pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
        {
            if(pThread->ifrunning == false)  
            {  
                pThread->ifrunning = true;  
                ngx_log_error_core(NGX_LOG_DEBUG, 0, "线程 %lu 已进入等待状态", tid);  
            } 
            pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex); //整个服务器程序刚初始化的时候，所有线程卡在这里等待；
        }

        //先判断线程退出这个条件
        if(m_shutdown)
        {   
            pthread_mutex_unlock(&m_pthreadMutex); //解锁互斥量
            ngx_log_error_core(NGX_LOG_INFO, 0, "线程 %lu 收到退出信号，即将退出", tid);  
            break;                     
        }

        //取消息进行处理【消息队列中有消息】
        char *jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();     //返回第一个元素但不检查元素存在与否
        pThreadPoolObj->m_MsgRecvQueue.pop_front();                //移除第一个元素但不返回	
        --pThreadPoolObj->m_iRecvMsgQueueCount;                    //收消息队列数字-1

        ngx_log_error_core(NGX_LOG_DEBUG, 0, "线程 %lu 取得消息，当前队列剩余消息数: %d", tid, pThreadPoolObj->m_iRecvMsgQueueCount); 

        //解锁互斥量
        err = pthread_mutex_unlock(&m_pthreadMutex); 
        if(err != 0)  
        {  
            ngx_log_error_core(NGX_LOG_ERR, err, "线程 %lu 释放互斥锁失败，错误码: %d", tid, err);  
        }

        //有消息可以处理，开始处理
        ++pThreadPoolObj->m_iRunningThreadNum;    //原子+1【记录正在干活的线程数量增加1】
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "线程 %lu 开始处理消息，当前工作线程数: %d", tid, pThreadPoolObj->m_iRunningThreadNum.load());  
        
        g_socket.threadRecvProcFunc(jobbuf);     //处理消息队列中来的消息


        p_memory->FreeMemory(jobbuf);              //释放消息内存 
        --pThreadPoolObj->m_iRunningThreadNum;     //原子-1【记录正在干活的线程数量减少1】

        ngx_log_error_core(NGX_LOG_DEBUG, 0, "线程 %lu 完成消息处理，当前工作线程数: %d", tid, pThreadPoolObj->m_iRunningThreadNum.load());  
      
    } 

    ngx_log_error_core(NGX_LOG_INFO, 0, "线程 %lu 已退出", tid);
    return (void*)0;
}

//停止所有线程【等待结束线程池中所有线程，该函数返回后，所有线程池中线程都结束了】
void CThreadPool::StopAll() 
{
    if(m_shutdown == true)
    {
        ngx_log_error_core(NGX_LOG_INFO, 0, "线程池已经处于关闭状态，无需重复调用StopAll()");  
        return;
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始停止线程池中所有线程...");
    m_shutdown = true;

    //唤醒等待该条件【卡在pthread_cond_wait()的】的所有线程，要在改变条件状态以后再给线程发信号
    int err = pthread_cond_broadcast(&m_pthreadCond); 
    if(err != 0)
    {
        ngx_log_error_core(NGX_LOG_ERR, err, "唤醒所有等待线程失败，错误码: %d", err);  
        return;
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "已发送唤醒信号给所有线程");  

    // 等待所有线程结束  
    ngx_log_error_core(NGX_LOG_INFO, 0, "等待所有线程结束...");  

    //等等线程，让线程返回    
    std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); //等待一个线程终止
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "线程 %lu 已成功结束", (*iter)->_Handle);
    }

    //所有的线程池中的线程都返回了；
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);    

    //释放一下new出来的ThreadItem【线程池中的线程】  
    int thread_count = 0;   
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if(*iter)
        {
			delete *iter;
	        thread_count++;
    }
    }
	m_threadVector.clear();

    ngx_log_error_core(NGX_LOG_INFO, 0, "线程池已完全停止，共释放 %d 个线程对象", thread_count);  
    return;     
}

//--------------------------------------------------------------------------------------
//收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
    //互斥
    int err = pthread_mutex_lock(&m_pthreadMutex);     
    if(err != 0)  
    {  
        ngx_log_error_core(NGX_LOG_ERR, err, "消息入队时获取互斥锁失败，错误码: %d", err);  
    }
        
    m_MsgRecvQueue.push_back(buf);	         //入消息队列
    ++m_iRecvMsgQueueCount;                  //收消息队列数字+1

    ngx_log_error_core(NGX_LOG_DEBUG, 0, "新消息已入队，当前队列消息数: %d", m_iRecvMsgQueueCount);  
    
    //取消互斥
    err = pthread_mutex_unlock(&m_pthreadMutex);   
    if(err != 0)  
    {  
        ngx_log_error_core(NGX_LOG_ERR, err, "消息入队后释放互斥锁失败，错误码: %d", err);  
    }

    //激发一个线程来干活
    Call();                                  
    return;
}

//来任务了，调一个线程池中的线程来干活
void CThreadPool::Call()
{
    int err = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是唤醒卡在pthread_cond_wait()的线程
    if(err != 0)  
    {  
        ngx_log_error_core(NGX_LOG_ERR, err, "唤醒线程失败，错误码: %d", err);  
    }  
    else  
    {  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "已唤醒一个线程处理消息");  
    } 
    
    if(m_iThreadNum == m_iRunningThreadNum) //线程池中线程总量，跟当前正在干活的线程数量一样，说明线程不够用了
    {        
     
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10)
        {
            m_iLastEmgTime = currtime;  //更新时间
            ngx_log_error_core(NGX_LOG_WARN, 0, "线程池负载过高，所有线程(%d个)均处于忙碌状态，建议扩容线程池", m_iThreadNum);
        }
    } //end if 

    return;
}

