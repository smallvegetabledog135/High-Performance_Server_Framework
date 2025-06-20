#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//构造函数
CSocekt::CSocekt()
{
    //配置相关
    m_worker_connections = 1;      //epoll连接最大数
    m_ListenPortCount = 1;         //监听一个端口
    m_RecyConnectionWaitTime = 60; //等待回收连接时间

    //epoll相关
    m_epollhandle = -1;          //epoll返回的句柄

    //网络通讯常用变量值
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);    //包头的sizeof值【占用的字节数】
    m_iLenMsgHeader =  sizeof(STRUC_MSG_HEADER);  //消息头的sizeof值【占用的字节数】

    //各种队列相关
    m_iSendMsgQueueCount     = 0;     //发消息队列大小
    m_totol_recyconnection_n = 0;     //待释放连接队列大小
    m_cur_size_              = 0;     //当前计时队列尺寸
    m_timer_value_           = 0;     //当前计时队列头部的时间值
    m_iDiscardSendPkgCount   = 0;     //丢弃的发送数据包数量

    //在线用户相关
    m_onlineUserCount        = 0;     //在线用户数量统计，先给0  
    m_lastprintTime          = 0;     //上次打印统计信息的时间，先给0

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::CSocekt()构造函数成功");

    return;	
}


bool CSocekt::Initialize()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize()开始执行");  
    
    ReadConf();  //读配置项  
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize()中的配置读取完毕，worker_connections=%d，ListenPortCount=%d", m_worker_connections, m_ListenPortCount);  
    
    if(ngx_open_listening_sockets() == false)  //打开监听端口    
    {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize()中打开监听端口失败");  
        return false;  
    }  
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize()成功返回");  
    return true;  
}

//子进程中需要执行的初始化函数
bool CSocekt::Initialize_subproc()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize_subproc()开始执行"); 

    //发消息互斥量初始化
    if(pthread_mutex_init(&m_sendMessageQueueMutex, NULL)  != 0)
    {        
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中发送消息互斥量初始化失败");
        return false;    
    }
    //连接相关互斥量初始化
    if(pthread_mutex_init(&m_connectionMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_connectionMutex)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中连接互斥量初始化失败");  
        return false;    
    }    
    //连接回收队列相关互斥量初始化
    if(pthread_mutex_init(&m_recyconnqueueMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中连接回收队列互斥量初始化失败");  
        return false;    
    } 
    //和时间处理队列有关的互斥量初始化
    if(pthread_mutex_init(&m_timequeueMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_timequeueMutex)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中时间队列互斥量初始化失败");  
        return false;    
    }
   
    if(sem_init(&m_semEventSendQueue,0,0) == -1)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中sem_init(&m_semEventSendQueue,0,0)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中发送消息信号量初始化失败");  
        return false;
    }

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize_subproc()中所有互斥量和信号量初始化成功");

    //创建线程
    int err;
    ThreadItem *pSendQueue;    //专门用来发送数据的线程
    m_threadVector.push_back(pSendQueue = new ThreadItem(this));                         //创建一个新线程对象并入到容器中 
    err = pthread_create(&pSendQueue->_Handle, NULL, ServerSendQueueThread,pSendQueue); //创建线程，错误不返回到errno，一般返回错误码
    if(err != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_create(ServerSendQueueThread)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中创建发送数据线程失败，返回值=%d", err);  
        return false;
    }

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize_subproc()中创建发送数据线程成功");

    ThreadItem *pRecyconn;    //专门用来回收连接的线程
    m_threadVector.push_back(pRecyconn = new ThreadItem(this)); 
    err = pthread_create(&pRecyconn->_Handle, NULL, ServerRecyConnectionThread,pRecyconn);
    if(err != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_create(ServerRecyConnectionThread)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中创建回收连接线程失败，返回值=%d", err);  
        return false;
    }

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize_subproc()中创建回收连接线程成功");

    if(m_ifkickTimeCount == 1)  //是否开启踢人时钟，1：开启   0：不开启
    {
        ThreadItem *pTimemonitor;    //专门用来处理到期不发心跳包的用户踢出的线程
        m_threadVector.push_back(pTimemonitor = new ThreadItem(this)); 
        err = pthread_create(&pTimemonitor->_Handle, NULL, ServerTimerQueueMonitorThread,pTimemonitor);
        if(err != 0)
        {
            ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_create(ServerTimerQueueMonitorThread)失败.");  
            ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Initialize_subproc()中创建时间监控线程失败，返回值=%d", err);  
            return false;
        }
        ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize_subproc()中创建时间监控线程成功");
    }

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Initialize_subproc()成功返回，共创建了%d个线程", (int)m_threadVector.size());  
    return true;
}

//释放函数
CSocekt::~CSocekt()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::~CSocekt()开始执行");
    //释放内存
    std::vector<lpngx_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos) //vector
	{		
		delete (*pos); 
	}
	m_ListenSocketList.clear();    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::~CSocekt()释放了%d个监听端口", m_ListenPortCount);  
    return;
}

//关闭退出函数
void CSocekt::Shutdown_subproc()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Shutdown_subproc()开始执行");
    if(sem_post(&m_semEventSendQueue)==-1) 
    {
         ngx_log_stderr(0,"CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");  
         ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::Shutdown_subproc()中发送消息信号量通知失败");  
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Shutdown_subproc()开始等待各个线程终止"); 
    std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); //等待一个线程终止
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Shutdown_subproc()所有线程已终止");
    //释放new出来的ThreadItem【线程池中的线程】    
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if(*iter)
			delete *iter;
	}
	m_threadVector.clear();
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Shutdown_subproc()所有线程对象已释放");
    //队列相关
    clearMsgSendQueue();
    clearconnection();
    clearAllFromTimerQueue();
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Shutdown_subproc()所有队列已清空");
    
    //多线程相关    
    pthread_mutex_destroy(&m_connectionMutex);          //连接相关互斥量释放
    pthread_mutex_destroy(&m_sendMessageQueueMutex);    //发消息互斥量释放    
    pthread_mutex_destroy(&m_recyconnqueueMutex);       //连接回收队列相关的互斥量释放
    pthread_mutex_destroy(&m_timequeueMutex);           //时间处理队列相关的互斥量释放
    sem_destroy(&m_semEventSendQueue);                  //发消息相关线程信号量释放
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::Shutdown_subproc()所有互斥量和信号量已释放");
}

//清理TCP发送消息队列
void CSocekt::clearMsgSendQueue()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::clearMsgSendQueue()开始执行，当前队列大小=%d", m_iSendMsgQueueCount.load());
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();
    int clearCount = 0;
	
	while(!m_MsgSendQueue.empty())
	{
		sTmpMempoint = m_MsgSendQueue.front();
		m_MsgSendQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
    m_iSendMsgQueueCount = 0;  
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::clearMsgSendQueue()执行完毕，共清理%d条消息", clearCount);
}

//读各种配置项
void CSocekt::ReadConf()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ReadConf()开始读取配置");
    CConfig *p_config = CConfig::GetInstance();
    m_worker_connections      = p_config->GetIntDefault("worker_connections",m_worker_connections);              //epoll连接的最大项数
    m_ListenPortCount         = p_config->GetIntDefault("ListenPortCount",m_ListenPortCount);                    //取得要监听的端口数量
    m_RecyConnectionWaitTime  = p_config->GetIntDefault("Sock_RecyConnectionWaitTime",m_RecyConnectionWaitTime); //等待这么些秒后才回收连接

    m_ifkickTimeCount         = p_config->GetIntDefault("Sock_WaitTimeEnable",0);                                //是否开启踢人时钟，1：开启   0：不开启
	m_iWaitTime               = p_config->GetIntDefault("Sock_MaxWaitTime",m_iWaitTime);                         //多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用	
	m_iWaitTime               = (m_iWaitTime > 5)?m_iWaitTime:5;                                                 //不建议低于5秒钟，因为无需太频繁
    m_ifTimeOutKick           = p_config->GetIntDefault("Sock_TimeOutKick",0);                                   //当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用 

    m_floodAkEnable          = p_config->GetIntDefault("Sock_FloodAttackKickEnable",0);                          //Flood攻击检测是否开启,1：开启   0：不开启
	m_floodTimeInterval      = p_config->GetIntDefault("Sock_FloodTimeInterval",100);                            //表示每次收到数据包的时间间隔是100(毫秒)
	m_floodKickCount         = p_config->GetIntDefault("Sock_FloodKickCounter",10);                              //累积多少次踢出此人

    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ReadConf()配置读取完毕: worker_connections=%d, ListenPortCount=%d, RecyTime=%d秒",   
                      m_worker_connections, m_ListenPortCount, m_RecyConnectionWaitTime);  
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ReadConf()踢人策略: 开启=%d, 检测间隔=%d秒, 超时踢出=%d",   
                      m_ifkickTimeCount, m_iWaitTime, m_ifTimeOutKick);  
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ReadConf()洪水攻击检测: 开启=%d, 时间间隔=%d毫秒, 阈值=%d次",   
                      m_floodAkEnable, m_floodTimeInterval, m_floodKickCount); 
    return;
}

//监听端口【支持多个端口】
//在创建worker进程之前执行这个函数；
bool CSocekt::ngx_open_listening_sockets()
{    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_open_listening_sockets()开始监听%d个端口", m_ListenPortCount);
    int                isock;                //socket
    struct sockaddr_in serv_addr;            //服务器的地址结构体
    int                iport;                //端口
    char               strinfo[100];         //临时字符串 
   
    //初始化相关
    memset(&serv_addr,0,sizeof(serv_addr));  //先初始化一下
    serv_addr.sin_family = AF_INET;                //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); //监听本地所有的IP地址

    //配置信息
    CConfig *p_config = CConfig::GetInstance();
    for(int i = 0; i < m_ListenPortCount; i++) //要监听这么多个端口
    {        
      
        isock = socket(AF_INET,SOCK_STREAM,0); //系统函数，成功返回非负描述符，出错返回-1
        if(isock == -1)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中socket()失败,i=%d.", i);  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_open_listening_sockets()创建socket失败，index=%d", i);  
            return false;  
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_open_listening_sockets()创建socket成功, fd=%d", isock);  

        int reuseaddr = 1;  //打开对应的设置项
        if(setsockopt(isock,SOL_SOCKET, SO_REUSEADDR,(const void *) &reuseaddr, sizeof(reuseaddr)) == -1)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.", i);  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_open_listening_sockets()设置SO_REUSEADDR失败，fd=%d", isock);  
            close(isock);                                                 
            return false;
        }

        int reuseport = 1;
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT,(const void *) &reuseport, sizeof(int))== -1) //端口复用需要内核支持
        { 
            ngx_log_stderr(errno, "CSocekt::Initialize()中setsockopt(SO_REUSEPORT)失败", i);  
            ngx_log_error_core(NGX_LOG_WARN, errno, "CSocekt::ngx_open_listening_sockets()设置SO_REUSEPORT失败，fd=%d", isock);  
        }

        //设置该socket为非阻塞
        if(setnonblocking(isock) == false)
        {                
            ngx_log_stderr(errno, "CSocekt::Initialize()中setnonblocking()失败,i=%d.", i);  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_open_listening_sockets()设置非阻塞失败，fd=%d", isock);  
            close(isock);
            return false;
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_open_listening_sockets()设置非阻塞成功，fd=%d", isock);  


        //设置本服务器要监听的地址和端口，这样客户端才能连接到该地址和端口并发送数据        
        strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = p_config->GetIntDefault(strinfo,10000);
        serv_addr.sin_port = htons((in_port_t)iport);  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_open_listening_sockets()准备绑定端口%d", iport);  

        //绑定服务器地址结构体
        if(bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中bind()失败,i=%d.", i);  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_open_listening_sockets()绑定端口%d失败，fd=%d", iport, isock);  
            close(isock);
            return false;
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_open_listening_sockets()绑定端口%d成功，fd=%d", iport, isock);  
        
        
        //开始监听
        if(listen(isock,NGX_LISTEN_BACKLOG) == -1)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中listen()失败,i=%d.", i);  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_open_listening_sockets()监听端口%d失败，fd=%d", iport, isock);  
            close(isock);
            return false;
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_open_listening_sockets()监听端口%d成功，fd=%d", iport, isock);  

        lpngx_listening_t p_listensocketitem = new ngx_listening_t; 
        memset(p_listensocketitem,0,sizeof(ngx_listening_t));      
        p_listensocketitem->port = iport;                          //记录下所监听的端口号
        p_listensocketitem->fd   = isock;                          //套接字木柄保存下来   
        ngx_log_error_core(NGX_LOG_INFO,0,"监听%d端口成功!",iport); 
        m_ListenSocketList.push_back(p_listensocketitem);          //加入到队列中
    }    
    if(m_ListenSocketList.size() <= 0) 
        {ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::ngx_open_listening_sockets()监听端口失败，没有成功监听任何端口");  
        return false;}
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_open_listening_sockets()监听端口完毕，共监听%d个端口成功", (int)m_ListenSocketList.size());  
    return true;
}

//设置socket连接为非阻塞模式
bool CSocekt::setnonblocking(int sockfd) 
{    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::setnonblocking()设置socket为非阻塞模式，fd=%d", sockfd);  
    
    int nb=1; //0：清除，1：设置  
    if(ioctl(sockfd, FIONBIO, &nb) == -1) //FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置  
    {  
        ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::setnonblocking()设置非阻塞模式失败，fd=%d", sockfd);  
        return false;  
    }  
    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::setnonblocking()设置非阻塞模式成功，fd=%d", sockfd);  
    return true;

}

//关闭socket
void CSocekt::ngx_close_listening_sockets()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_close_listening_sockets()开始关闭监听socket，共%d个", m_ListenPortCount);  
    
    for(int i = 0; i < m_ListenPortCount; i++)   
    {  
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_close_listening_sockets()关闭第%d个监听socket，fd=%d，端口=%d",   
                          i, m_ListenSocketList[i]->fd, m_ListenSocketList[i]->port);   
        close(m_ListenSocketList[i]->fd);  
        ngx_log_error_core(NGX_LOG_INFO, 0, "关闭监听端口%d!", m_ListenSocketList[i]->port); //显示一些信息到日志中  
    }  
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_close_listening_sockets()监听socket已全部关闭");  
    return;
}

//将一个待发送消息入到发消息队列中
void CSocekt::msgSend(char *psendbuf) 
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::msgSend()开始处理待发送消息，当前队列大小=%d", m_iSendMsgQueueCount.load());  
    CMemory *p_memory = CMemory::GetInstance();
    CLock lock(&m_sendMessageQueueMutex);  //互斥量

    //发送消息队列过大
    if(m_iSendMsgQueueCount > 50000)
    {
        m_iDiscardSendPkgCount++;
        p_memory->FreeMemory(psendbuf);
        ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::msgSend()发送队列已超过最大限制(50000)，丢弃消息，当前丢弃计数=%d", m_iDiscardSendPkgCount);  
		return;
    }
        
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)psendbuf;
	lpngx_connection_t p_Conn = pMsgHeader->pConn;
    if(p_Conn->iSendCount > 400)
    {
        ngx_log_stderr(0,"CSocekt::msgSend()中发现某用户%d积压了大量待发送数据包，切断与他的连接！",p_Conn->fd);  
        ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::msgSend()发现用户(fd=%d)积压了大量待发送数据包(>400)，判定为恶意用户，切断连接", p_Conn->fd);  
        m_iDiscardSendPkgCount++;
        p_memory->FreeMemory(psendbuf);
        zdClosesocketProc(p_Conn); //直接关闭
		return;
    }

    ++p_Conn->iSendCount; //发送队列中有的数据条目数+1；
    m_MsgSendQueue.push_back(psendbuf);     
    ++m_iSendMsgQueueCount;   //原子操作
  
     ngx_log_error_core(NGX_LOG_ERR, 0, "CSocekt::msgSend()连接 %d 通过epoll添加发送事件失败，已发送队列数=%d，当前队列大小=%d，但这种情况通常不会发生",   
                  p_Conn->fd, p_Conn->iSendCount.load(), m_iSendMsgQueueCount.load());   

    //将信号量的值+1
    if(sem_post(&m_semEventSendQueue)==-1)
    {
        ngx_log_stderr(0,"CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");  
        ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::msgSend()发送通知信号量失败");  
    }
    return;
}

void CSocekt::zdClosesocketProc(lpngx_connection_t p_Conn)
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::zdClosesocketProc()开始关闭连接，fd=%d", p_Conn->fd);  
    
    if(m_ifkickTimeCount == 1)
    {
        DeleteFromTimerQueue(p_Conn); //从时间队列中把连接干掉
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::zdClosesocketProc()已从时间队列中删除连接，fd=%d", p_Conn->fd);  
    }
    if(p_Conn->fd != -1)
    {   
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::zdClosesocketProc()准备关闭socket，fd=%d", p_Conn->fd);  
        close(p_Conn->fd); //这个socket关闭，关闭后epoll就会被从红黑树中删除，这之后无法收到任何epoll事件
        p_Conn->fd = -1;
    }

    if(p_Conn->iThrowsendCount > 0)  
        {--p_Conn->iThrowsendCount;   
         ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::zdClosesocketProc()减少iThrowsendCount计数，当前值=%d", p_Conn->iThrowsendCount.load());  
    
        }
    inRecyConnectQueue(p_Conn);
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::zdClosesocketProc()连接已放入回收队列");  
    
    return;
}

//测试是否flood攻击成立，成立则返回true，否则返回false
bool CSocekt::TestFlood(lpngx_connection_t pConn)
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::TestFlood()开始检测连接(fd=%d)是否存在洪水攻击", pConn->fd);  
    
    struct  timeval sCurrTime;   //当前时间结构
	uint64_t        iCurrTime;   //当前时间
	bool  reco      = false;
	
	gettimeofday(&sCurrTime, NULL); //取得当前时间
    iCurrTime =  (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);  
	if((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)   
	{
        //发包太频繁记录
		pConn->FloodAttackCount++;
		pConn->FloodkickLastTime = iCurrTime;
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::TestFlood()检测到连接(fd=%d)发包过于频繁，间隔=%d毫秒，计数增加到%d",   
                          pConn->fd, (int)(iCurrTime - pConn->FloodkickLastTime), pConn->FloodAttackCount);  
    
	}
	else
	{
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::TestFlood()连接(fd=%d)发包间隔正常，间隔=%d毫秒，重置计数",   
                          pConn->fd, (int)(iCurrTime - pConn->FloodkickLastTime));
		pConn->FloodAttackCount = 0;
		pConn->FloodkickLastTime = iCurrTime;
	}

	if(pConn->FloodAttackCount >= m_floodKickCount)
	{
		//可以踢此人的标志
		reco = true;
        ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::TestFlood()检测到连接(fd=%d)发起洪水攻击，攻击计数=%d，阈值=%d",   
                          pConn->fd, pConn->FloodAttackCount, m_floodKickCount);  
    
	}
	return reco;
}

//打印统计信息
void CSocekt::printTDInfo()
{
    //return;
    time_t currtime = time(NULL);
    if( (currtime - m_lastprintTime) > 10)
    {
        ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::printTDInfo()开始打印统计信息");  
        //超过10秒打印一次
        int tmprmqc = g_threadpool.getRecvMsgQueueCount(); //收消息队列

        m_lastprintTime = currtime;
        int tmpoLUC = m_onlineUserCount;   
        int tmpsmqc = m_iSendMsgQueueCount; 
        ngx_log_stderr(0,"------------------------------------begin--------------------------------------");
        ngx_log_stderr(0,"当前在线人数/总人数(%d/%d)。",tmpoLUC,m_worker_connections);        
        ngx_log_stderr(0,"连接池中空闲连接/总连接/要释放的连接(%d/%d/%d)。",m_freeconnectionList.size(),m_connectionList.size(),m_recyconnectionList.size());
        ngx_log_stderr(0,"当前时间队列大小(%d)。",m_timerQueuemap.size());        
        ngx_log_stderr(0,"当前收消息队列/发消息队列大小分别为(%d/%d)，丢弃的待发送数据包数量为%d。",tmprmqc,tmpsmqc,m_iDiscardSendPkgCount);        
        
        ngx_log_error_core(NGX_LOG_INFO, 0, "统计信息：在线人数=%d/%d，连接池=%d/%d/%d，时间队列=%d，消息队列=%d/%d，丢弃包数=%d",   
                          tmpoLUC, m_worker_connections,   
                          m_freeconnectionList.size(), m_connectionList.size(), m_recyconnectionList.size(),  
                          m_timerQueuemap.size(),   
                          tmprmqc, tmpsmqc, m_iDiscardSendPkgCount);  
        
        if( tmprmqc > 100000)
        {
            ngx_log_stderr(0,"接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！",tmprmqc);  
            ngx_log_error_core(NGX_LOG_WARN, 0, "警告：接收队列条目数量过大(%d)，建议限速或增加处理线程数量", tmprmqc);  
        }
        ngx_log_stderr(0,"-------------------------------------end---------------------------------------");
    }
    return;
}

//epoll功能初始化，子进程中进行，本函数被ngx_worker_process_init()所调用
int CSocekt::ngx_epoll_init()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_epoll_init()开始初始化epoll");  
    m_epollhandle = epoll_create(m_worker_connections);    
    if (m_epollhandle == -1) 
    {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中epoll_create()失败.");  
        ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_epoll_init()创建epoll对象失败");  
        exit(2); 
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()创建epoll对象成功，句柄=%d", m_epollhandle);  

    //创建连接池
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()开始初始化连接池");  
    initconnection();  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()连接池初始化完成，连接池大小=%d", m_connectionList.size());  
    
    //遍历所有监听socket【监听端口】，为每个监听socket增加一个连接池中的连接
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()开始将监听socket添加到epoll中，监听端口数=%d", m_ListenSocketList.size());  
    std::vector<lpngx_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()处理监听socket，fd=%d", (*pos)->fd);  
        
        lpngx_connection_t p_Conn = ngx_get_connection((*pos)->fd); //从连接池中获取一个空闲连接对象
        if (p_Conn == NULL)
        {
            ngx_log_stderr(errno, "CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");  
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_epoll_init()获取连接对象失败，连接池可能已空");  
            exit(2); 
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()成功获取连接对象，开始关联监听对象");  
        
        p_Conn->listening = (*pos);   //连接对象 和监听对象关联，方便通过连接对象找监听对象
        (*pos)->connection = p_Conn;  //监听对象 和连接对象关联，方便通过监听对象找连接对象

        p_Conn->rhandler = &CSocekt::ngx_event_accept;
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()设置读事件处理函数，准备添加到epoll中");  

        if(ngx_epoll_oper_event(
                                (*pos)->fd,         //socekt句柄
                                EPOLL_CTL_ADD,      //事件类型，这里是增加
                                EPOLLIN|EPOLLRDHUP, //标志
                                0,                 
                                p_Conn              //连接池中的连接 
                                ) == -1) 
        {
            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_epoll_init()添加监听socket到epoll失败，fd=%d", (*pos)->fd);  
            exit(2); 
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_init()成功将监听socket添加到epoll，fd=%d", (*pos)->fd);
    } 
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_epoll_init()初始化epoll成功，epoll_fd=%d", m_epollhandle);  
    return 1;
}

//对epoll事件的具体处理
//返回值：成功返回1，失败返回-1；
int CSocekt::ngx_epoll_oper_event(
                        int                fd,               //句柄，一个socket
                        uint32_t           eventtype,        //事件类型，EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL 
                        uint32_t           flag,             //标志
                        int                bcaction,         //补充动作，用于补充flag标记的不足  :  0：增加   1：去掉 2：完全覆盖 
                        lpngx_connection_t pConn             //pConn：一个指针【一个连接】
                        )
{
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()开始操作epoll事件，fd=%d，事件类型=%ud", fd, eventtype);  
    
    struct epoll_event ev;    
    memset(&ev, 0, sizeof(ev));
    if(eventtype == EPOLL_CTL_ADD) //往红黑树中增加节点；
    {
        //红黑树从无到有增加节点
        ev.events = flag;      
        pConn->events = flag; 
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()添加事件到epoll，fd=%d，flag=%ud", fd, flag);  
    
    }
    else if(eventtype == EPOLL_CTL_MOD)
    {
        //节点已经在红黑树中，修改节点的事件信息
        ev.events = pConn->events; 
        if(bcaction == 0)
        {
            //增加某个标记            
            ev.events |= flag;
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()修改事件，增加标记，fd=%d，原flag=%ud，新flag=%ud",   
                              fd, pConn->events, ev.events);  
        
        }
        else if(bcaction == 1)
        {
            //去掉某个标记
            ev.events &= ~flag;
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()修改事件，去掉标记，fd=%d，原flag=%ud，新flag=%ud",   
                              fd, pConn->events, ev.events);  
        
        }
        else
        {
            //完全覆盖某个标记            
            ev.events = flag;      //完全覆盖
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()修改事件，完全覆盖标记，fd=%d，原flag=%ud，新flag=%ud",   
                              fd, pConn->events, flag);           
                    
        }
        pConn->events = ev.events; //记录该标记
    }
    else
    {
        //删除红黑树中节点
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()删除事件，fd=%d", fd);  
        return  1;  
    } 

    ev.data.ptr = (void *)pConn;

    if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)
    {
        ngx_log_stderr(errno, "CSocekt::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.", fd, eventtype, flag, bcaction);    
        ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ngx_epoll_oper_event()操作epoll事件失败，fd=%d，事件类型=%ud", fd, eventtype);  
        return -1; 
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_oper_event()操作epoll事件成功，fd=%d，事件类型=%ud", fd, eventtype);  
    return 1;
}

//获取发生的事件消息
//参数unsigned int timer：epoll_wait()阻塞的时长
//返回值，1：正常返回  ,0：有问题返回
//本函数被ngx_process_events_and_timers()调用，ngx_process_events_and_timers()在子进程的死循环中被反复调用
int CSocekt::ngx_epoll_process_events(int timer) 
{   
    //等待事件，事件会返回到m_events里，最多返回NGX_MAX_EVENTS个事件
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()等待epoll事件，最大等待时间=%d毫秒", timer);  
    int events = epoll_wait(m_epollhandle, m_events, NGX_MAX_EVENTS, timer);    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()epoll_wait返回事件数：%d", events); 
    if(events == -1)
    {
        //有错误发生
        if(errno == EINTR) 
        {
            //信号所致
            ngx_log_error_core(NGX_LOG_INFO, errno, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败，被信号中断!");   
            return 1; 
        }
        else
        {
            ngx_log_error_core(NGX_LOG_ALERT, errno, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");   
            return 0; 
        }
    }

    if(events == 0) 
    {
        if(timer != -1)
        {
            //阻塞到时间 
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()epoll_wait超时，无事件");  
            return 1;
        }     
        ngx_log_error_core(NGX_LOG_ALERT, 0, "CSocekt::ngx_epoll_process_events()中epoll_wait()无限等待却没返回任何事件!");   
        return 0; 
    }

    //有事件收到
    lpngx_connection_t p_Conn;
    uint32_t           revents;
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ngx_epoll_process_events()开始处理事件，事件数=%d", events);  

    for(int i = 0; i < events; ++i)    //遍历本次epoll_wait返回的所有事件
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()处理第%d个事件", i);  
        p_Conn = (lpngx_connection_t)(m_events[i].data.ptr);           //ngx_epoll_add_event()给进去的，这里能取出来
        //开始处理
        revents = m_events[i].events;//取出事件类型
        
        if(revents & EPOLLIN)  //如果是读事件
        {
          ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()处理读事件，fd=%d", p_Conn->fd);  
            
          (this->* (p_Conn->rhandler) )(p_Conn); 
        }
        
        if(revents & EPOLLOUT) //如果是写事件
        {

           ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()处理写事件，fd=%d", p_Conn->fd);  
            if(revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) //客户端关闭
            {
               ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()客户端关闭连接，fd=%d", p_Conn->fd);  
               --p_Conn->iThrowsendCount;                 
            }
            else
            {
                (this->* (p_Conn->whandler) )(p_Conn);   
            }            
        }
    }      
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()事件处理完毕，共处理%d个事件", events);  
    return 1; 
}

//处理发送消息队列的线程
void* CSocekt::ServerSendQueueThread(void* threadData)
{    
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocekt *pSocketObj = pThread->_pThis;
    int err;
    std::list <char *>::iterator pos,pos2,posend;
    
    char *pMsgBuf;	
    LPSTRUC_MSG_HEADER	pMsgHeader;
	LPCOMM_PKG_HEADER   pPkgHeader;
    lpngx_connection_t  p_Conn;
    unsigned short      itmp;
    ssize_t             sendsize;  

    CMemory *p_memory = CMemory::GetInstance();
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ServerSendQueueThread()线程开始运行");  
    
    while(g_stopEvent == 0) //不退出
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()等待信号量");  
        if(sem_wait(&pSocketObj->m_semEventSendQueue) == -1)
        {
            if(errno != EINTR) 
                ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");            
        }

        //处理数据收发
        if(g_stopEvent != 0)  //整个进程退出  
        {  
            ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ServerSendQueueThread()收到程序退出信号，线程退出");  
            break;  
        }  

        if(pSocketObj->m_iSendMsgQueueCount > 0)  
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()发送消息队列中有%d条消息需要处理", pSocketObj->m_iSendMsgQueueCount.load());  
            
            err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);          
            if(err != 0)   
            {  
                ngx_log_stderr(err, "CSocekt::ServerSendQueueThread()中pthread_mutex_lock()失败，返回的错误码为%d!", err);  
                ngx_log_error_core(NGX_LOG_ERR, err, "CSocekt::ServerSendQueueThread()中pthread_mutex_lock()失败");  
            }
            pos    = pSocketObj->m_MsgSendQueue.begin();
			posend = pSocketObj->m_MsgSendQueue.end();

            while(pos != posend)
            {
                pMsgBuf = (*pos);                          //拿到的每个消息都是消息头+包头+包体
                pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;  //指向消息头
                pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+pSocketObj->m_iLenMsgHeader);//指向包头
                p_Conn = pMsgHeader->pConn;

                ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()处理一条消息，目标连接fd=%d", p_Conn->fd);  
              
                if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence) 
                {
                    ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ServerSendQueueThread()连接序列号已变化，丢弃消息，连接fd=%d，序列号：%d!=%d",   
                                     p_Conn->fd, pMsgHeader->iCurrsequence, p_Conn->iCurrsequence);  
                    
                    pos2=pos;
                    pos++;
                    pSocketObj->m_MsgSendQueue.erase(pos2);
                    --pSocketObj->m_iSendMsgQueueCount; 	
                    p_memory->FreeMemory(pMsgBuf);	
                    continue;
                }

                if(p_Conn->iThrowsendCount > 0) 
                {
                    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()连接fd=%d已由系统驱动发送，本次跳过", p_Conn->fd);  
                    pos++;
                    continue;
                }

                --p_Conn->iSendCount; 

                p_Conn->psendMemPointer = pMsgBuf;    
                pos2=pos;
				pos++;
                pSocketObj->m_MsgSendQueue.erase(pos2);
                --pSocketObj->m_iSendMsgQueueCount;      
                p_Conn->psendbuf = (char *)pPkgHeader;   
                itmp = ntohs(pPkgHeader->pkgLen);        
                p_Conn->isendlen = itmp;                 
                                
                ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()准备发送数据，连接fd=%d，数据长度=%d", p_Conn->fd, p_Conn->isendlen);  

                sendsize = pSocketObj->sendproc(p_Conn,p_Conn->psendbuf,p_Conn->isendlen); 
                if(sendsize > 0)
                {                    
                    if(sendsize == p_Conn->isendlen) //成功发送数据
                    {
                        //成功发送的和要求发送的数据相等，说明全部发送成功了
                        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()数据发送完毕，连接fd=%d，发送长度=%d", p_Conn->fd, sendsize);  
                        
                        p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
                        p_Conn->psendMemPointer = NULL;
                        p_Conn->iThrowsendCount = 0;                          
                        }
                    else  //没有全部发送完毕(EAGAIN)，数据只发出去了一部分，因为 发送缓冲区满了
                    {                        
                        p_Conn->psendbuf = p_Conn->psendbuf + sendsize;
				        p_Conn->isendlen = p_Conn->isendlen - sendsize;
                        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()数据部分发送，连接fd=%d，已发送=%d，剩余=%d",   
                                         p_Conn->fd, sendsize, p_Conn->isendlen);  
                        	
                        //因为发送缓冲区慢了，所以现在要依赖系统通知来发送数据
                        ++p_Conn->iThrowsendCount;             //标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
                        //投递此事件后，将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
                        ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()投递写事件到epoll，连接fd=%d", p_Conn->fd);  
                        
                        if(pSocketObj->ngx_epoll_oper_event(
                                p_Conn->fd,         //socket句柄
                                EPOLL_CTL_MOD,      //事件类型
                                EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_Conn              //连接池中的连接
                                ) == -1)
                        {
                            ngx_log_stderr(errno, "CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");  
                            ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ServerSendQueueThread()投递写事件到epoll失败，连接fd=%d", p_Conn->fd);  
                        }
                    } 
                    continue;  //继续处理其他消息                    
                } 

                else if(sendsize == 0)
                {
                    ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ServerSendQueueThread()sendproc()返回0，连接fd=%d可能已断开", p_Conn->fd);  
                    
                    p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0;   
                    continue;
                }

                else if(sendsize == -1)
                {
                    //发送缓冲区已经满了
                    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()发送缓冲区已满，连接fd=%d", p_Conn->fd);  
                    ++p_Conn->iThrowsendCount; //标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
                    //投递此事件后，将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
                    ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()投递写事件到epoll，连接fd=%d", p_Conn->fd);  
                    
                    if(pSocketObj->ngx_epoll_oper_event(
                                p_Conn->fd,         //socket句柄
                                EPOLL_CTL_MOD,      //事件类型
                                EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_Conn              //连接池中的连接
                                ) == -1)
                    {
                        ngx_log_stderr(errno, "CSocekt::ServerSendQueueThread()中ngx_epoll_add_event()_2失败.");  
                        ngx_log_error_core(NGX_LOG_ERR, errno, "CSocekt::ServerSendQueueThread()投递写事件到epoll失败，连接fd=%d", p_Conn->fd);  
                    }
                    continue;
                }

                else
                {
                    ngx_log_error_core(NGX_LOG_WARN, 0, "CSocekt::ServerSendQueueThread()sendproc()返回-2，连接fd=%d已断开", p_Conn->fd);
                    p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0;  
                    continue;
                }

            } 

            err = pthread_mutex_unlock(&pSocketObj->m_sendMessageQueueMutex); 
            if(err != 0)  {  
                ngx_log_stderr(err, "CSocekt::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);  
                ngx_log_error_core(NGX_LOG_ERR, err, "CSocekt::ServerSendQueueThread()解锁互斥量失败");  
            }  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ServerSendQueueThread()消息队列处理完毕");  
            
        } 
    } 
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "CSocekt::ServerSendQueueThread()线程退出");  
    return (void*)0;
}
