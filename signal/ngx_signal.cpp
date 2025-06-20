
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>    //信号相关头文件 
#include <errno.h>     //errno
#include <sys/wait.h>  //waitpid

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h" 

//信号相关的结构ngx_signal_t
typedef struct 
{
    int           signo;       //信号对应的数字编号 
    const  char   *signame;    //信号对应的中文名字

    //信号处理函数
    void  (*handler)(int signo, siginfo_t *siginfo, void *ucontext); //函数指针,   siginfo_t:系统定义的结构
} ngx_signal_t;

//声明一个信号处理函数
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext); 
static void ngx_process_get_status(void);                                      //获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程

ngx_signal_t  signals[] = {
    // signo      signame             handler
    { SIGHUP,    "SIGHUP",           ngx_signal_handler },        //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           ngx_signal_handler },        //标识2   
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },        //标识15
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        //标识3
    { SIGIO,     "SIGIO",            ngx_signal_handler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL               },        //想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果不忽略，进程会被操作系统杀死，--标识31
    { 0,         NULL,               NULL               }         
};

//初始化信号的函数，用于注册信号处理程序
//返回值：0成功  ，-1失败
int ngx_init_signals()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始初始化信号处理器");
    ngx_signal_t      *sig;  //指向自定义结构数组的指针 
    struct sigaction   sa;   //sigaction：系统定义的跟信号有关的一个结构
    int                success_count = 0;
    for (sig = signals; sig->signo != 0; sig++)  //将signo ==0作为一个标记
    {        
        memset(&sa,0,sizeof(struct sigaction));

        if (sig->handler)  //如果信号处理函数不为空
        {
            sa.sa_sigaction = sig->handler;  //sa_sigaction：指定信号处理程序(函数)
            sa.sa_flags = SA_SIGINFO;        
                                                
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "为信号 %d (%s) 设置自定义处理函数", sig->signo, sig->signame);
        }
        else
        {
            sa.sa_handler = SIG_IGN;                                        
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "设置忽略信号 %d (%s)", sig->signo, sig->signame);
        } 

        sigemptyset(&sa.sa_mask);   
        
        //设置信号处理动作(信号处理函数)
        if (sigaction(sig->signo, &sa, NULL) == -1) 
        {   
            ngx_log_error_core(NGX_LOG_EMERG, errno, "设置信号 %d (%s) 处理函数失败: %s",   
                              sig->signo, sig->signame, strerror(errno));
            return -1;
        }	
        else
        {            
            success_count++;  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "成功设置信号 %d (%s) 处理函数", sig->signo, sig->signame);
        }
    } 
    return 0;   
}

//信号处理函数
//siginfo：这个系统定义的结构中包含了信号产生原因的有关信息
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext)
{    
    //printf("来信号了\n");

    ngx_signal_t    *sig;    //自定义结构
    char            *action; //一个字符串，用于记录一个动作字符串以往日志文件中写
    
    for (sig = signals; sig->signo != 0; sig++) //遍历信号数组    
    {         
        //找到对应信号，即可处理
        if (sig->signo == signo) 
        { 
            break;
        }
    } 

    // 如果没找到对应的信号处理结构  
    if (sig->signo == 0) {  
        ngx_log_error_core(NGX_LOG_ALERT, 0, "收到未知信号 %d", signo);  
        return;  
    }  

    ngx_log_error_core(NGX_LOG_DEBUG, 0, "收到信号 %d (%s)", signo, sig->signame);  
    action = (char *)""; 

    if(ngx_process == NGX_PROCESS_MASTER)      //master进程，管理进程，处理的信号一般会比较多 
    {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "Master进程处理信号 %d (%s)", signo, sig->signame);

        //master进程的往这里走
        switch (signo)
        {
        case SIGCHLD:  //一般子进程退出会收到该信号
            ngx_log_error_core(NGX_LOG_INFO, 0, "Master进程收到子进程退出信号(SIGCHLD)");
            ngx_reap = 1;  //标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
            break;

        default:
            break;
        } //end switch
    }
    else if(ngx_process == NGX_PROCESS_WORKER) //worker进程，具体干活的进程，处理的信号相对比较少
    {

    }
    else
    {
        //do nothing
    } 

    if (siginfo && siginfo->si_pid)  
    {  
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "信号 %d (%s) 来自进程 %P%s",   
                          signo, sig->signame, siginfo->si_pid, action);  
    }  
    else  
    {  
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "信号 %d (%s) 已接收%s",   
                          signo, sig->signame, action);  
    }  

    // 处理子进程退出  
    if (signo == SIGCHLD)  
    {  
        ngx_log_error_core(NGX_LOG_INFO, 0, "开始处理子进程退出事件");  
        ngx_process_get_status();  
    }  

    return;  
}

//获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
static void ngx_process_get_status(void)
{
    pid_t            pid;
    int              status;
    int              err;
    int              one=0; 
    int    processed_count = 0; 

    ngx_log_error_core(NGX_LOG_INFO, 0, "开始获取子进程退出状态");  

    //当杀死一个子进程时，父进程会收到这个SIGCHLD信号。
    for ( ;; ) 
    {
        pid = waitpid(-1, &status, WNOHANG);        

        if(pid == 0) //子进程没结束，会立即返回这个数字
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "没有子进程退出，waitpid()返回0");  
            return;
        } 
         // waitpid出错  
        if (pid == -1)  
        {  
            err = errno;  
            
            if (err == EINTR)  
            {  
                ngx_log_error_core(NGX_LOG_INFO, err, "waitpid()被信号中断，继续尝试");  
                continue;  
            }  

            if (err == ECHILD && one)  
            {  
                ngx_log_error_core(NGX_LOG_INFO, err, "没有子进程需要等待，waitpid()返回ECHILD");  
                return;  
            }  

            if (err == ECHILD)  
            {  
                ngx_log_error_core(NGX_LOG_INFO, err, "没有子进程，waitpid()返回ECHILD");  
                return;  
            }  
            
            ngx_log_error_core(NGX_LOG_ALERT, err, "waitpid()调用失败: %s", strerror(err));  
            return;  
        }  
        //成功【返回进程id】
        one = 1; 
        processed_count++; 
        if(WTERMSIG(status))  //获取使子进程终止的信号编号
        {
            ngx_log_error_core(NGX_LOG_ALERT, 0, "子进程(pid=%P)因信号 %d 退出",   
                              pid, WTERMSIG(status)); 
        }
        else
        {
            ngx_log_error_core(NGX_LOG_NOTICE, 0, "子进程(pid=%P)正常退出，退出码: %d",   
                              pid, WEXITSTATUS(status)); 
        }
    } //end for

    ngx_log_error_core(NGX_LOG_INFO, 0, "子进程状态获取完成，共处理 %d 个子进程退出事件", processed_count);  
    return;
}
