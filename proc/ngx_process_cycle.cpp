#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums,const char *pprocname);
static void ngx_worker_process_cycle(int inum,const char *pprocname);
static void ngx_worker_process_init(int inum);

//变量声明
static u_char  master_process[] = "master process";

//创建worker子进程
void ngx_master_process_cycle()
{    
    sigset_t set;        //信号集

    ngx_log_error_core(NGX_LOG_INFO, 0, "Master进程开始运行，进入主循环"); 

    sigemptyset(&set);   //清空信号集

    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) 
    {        
        ngx_log_error_core(NGX_LOG_ALERT, errno, "主进程设置信号屏蔽失败: %s", strerror(errno));
    }
    else  
    {  
        ngx_log_error_core(NGX_LOG_INFO, 0, "主进程成功设置信号屏蔽");  
    } 

    //设置主进程标题begin
    size_t size;
    int    i;
    size = sizeof(master_process); 
    size += g_argvneedmem;            
    if(size < 1000) //长度小于这个，才设置标题
    {
        char title[1000] = {0};
        strcpy(title,(const char *)master_process); //"master process"
        strcat(title," ");  //"master process "
        for (i = 0; i < g_os_argc; i++)         //"master process ./nginx"
        {
            strcat(title,g_os_argv[i]);
        }
        ngx_setproctitle(title); //设置标题
        ngx_log_error_core(NGX_LOG_INFO, 0, "主进程开始设置进程标题"); 
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "主进程[%P]启动成功，标题：%s", ngx_pid, title); 
    }    
    //首先设置主进程标题end
        
    //从配置文件中读取要创建的worker进程数量
    CConfig *p_config = CConfig::GetInstance(); //单例类
    int workprocess = p_config->GetIntDefault("WorkerProcesses",1); //从配置文件中得到要创建的worker进程数量
    ngx_start_worker_processes(workprocess);  //创建worker子进程
    ngx_log_error_core(NGX_LOG_INFO, 0, "主进程准备创建 %d 个工作进程", workprocess);

    //创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来    
    sigemptyset(&set); //信号屏蔽字为空，表示不屏蔽任何信号
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程创建完成，主进程进入事件等待循环");

    for ( ;; ) 
    {
        sigsuspend(&set); //阻塞，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回）；
    }
    return;
}

//根据给定的参数创建指定数量的子进程
//threadnums:要创建的子进程数量
static void ngx_start_worker_processes(int threadnums)
{
    int i;
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始创建 %d 个工作进程", threadnums); 
    for (i = 0; i < threadnums; i++)  //master进程在这个循环，创建若干个子进程
    {
        ngx_spawn_process(i,"worker process");
    } 

    ngx_log_error_core(NGX_LOG_INFO, 0, "所有工作进程创建指令已发出");
    return;
}

//产生一个子进程
//inum：进程编号【0开始】
//pprocname：子进程名字"worker process"
static int ngx_spawn_process(int inum,const char *pprocname)
{
    pid_t  pid;
    ngx_log_error_core(NGX_LOG_INFO, 0, "正在创建工作进程 %d", inum);
    pid = fork(); //fork()系统调用产生子进程
    switch (pid)  //pid判断父子进程，分支处理
    {  
    case -1: 
    //产生子进程失败
        ngx_log_error_core(NGX_LOG_ERR, errno, "创建工作进程 %d 失败: %s", inum, strerror(errno));
        return -1;

    case 0:  //子进程分支
        ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 创建成功，进程ID: %d", inum, getpid());  
        ngx_parent = ngx_pid;              //因为是子进程，所有原来的pid变成父pid
        ngx_pid = getpid();                //重新获取pid,即本子进程的pid
        ngx_worker_process_cycle(inum,pprocname); 
        break;

    default:          
        ngx_log_error_core(NGX_LOG_INFO, 0, "主进程成功创建工作进程 %d，进程ID: %d", inum, pid);
        break;
    }

    //父进程分支走到这里，子进程流程不往下走
    return pid;
}

//worker子进程的功能函数，每个woker子进程，无限循环【处理网络事件和定时器事件以对外提供web服务】
//子进程分叉才会走到这里
//inum：进程编号【0开始】
static void ngx_worker_process_cycle(int inum,const char *pprocname) 
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 开始运行，进程ID: %d", inum, getpid());
    //设置一下变量
    ngx_process = NGX_PROCESS_WORKER;  //设置进程的类型，是worker进程

    //重新为子进程设置进程名，不与父进程重复
    // 初始化工作进程  
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 开始初始化", inum);
    ngx_worker_process_init(inum);
    ngx_setproctitle(pprocname); //设置标题   
    ngx_log_error_core(NGX_LOG_NOTICE, 0, "工作进程 %d [%P] 初始化完成，标题：%s", inum, ngx_pid, pprocname);

    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程进入主循环");
    for(;;)
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "工作进程 %d 等待事件...", inum);
        ngx_process_events_and_timers(); //处理网络事件和定时器事件
    } 

    //如果从这个循环跳出来
    ngx_log_error_core(NGX_LOG_WARN, 0, "工作进程 %d 意外退出主循环，准备清理资源", inum); 
    g_threadpool.StopAll();      //停止线程池；
    g_socket.Shutdown_subproc(); //socket需要释放的东西释放；
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 资源清理完毕，即将退出", inum);
    return;
}

//子进程创建时调用本函数进行初始化
static void ngx_worker_process_init(int inum)
{
    sigset_t  set;      //信号集
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 开始初始化信号处理", inum);
    sigemptyset(&set);  //清空信号集
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)  
    {
        ngx_log_error_core(NGX_LOG_ALERT, errno, "工作进程 %d 设置信号掩码失败: %s", inum, strerror(errno));
    }
    else  
    {  
        ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 信号掩码设置成功", inum);  
    }  

    //线程池代码
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 开始初始化线程池", inum);
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount",5); //处理接收到的消息的线程池中线程数量
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 将创建 %d 个线程", inum, tmpthreadnums);
    if(g_threadpool.Create(tmpthreadnums) == false)  //创建线程池中线程
    {
        ngx_log_error_core(NGX_LOG_ERR, 0, "工作进程 %d 创建线程池失败，退出", inum);
        exit(-2);
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 线程池创建成功", inum);
    sleep(1); 
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 开始初始化Socket", inum);
    if(g_socket.Initialize_subproc() == false) 
    {
        ngx_log_error_core(NGX_LOG_ERR, 0, "工作进程 %d 初始化Socket失败，退出", inum);
        exit(-2);
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d Socket初始化成功", inum);
    
    // Epoll初始化  
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 开始初始化Epoll", inum);  
    g_socket.ngx_epoll_init();  
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d Epoll初始化成功", inum);  
    ngx_log_error_core(NGX_LOG_INFO, 0, "工作进程 %d 初始化完成", inum);
    return;
}
