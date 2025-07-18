﻿
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h> 
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>          //gettimeofday

#include "ngx_macro.h"         //各种宏定义
#include "ngx_func.h"          //各种函数声明
#include "ngx_c_conf.h"        //和配置文件处理相关的类,名字带c_表示和类有关
#include "ngx_c_socket.h"      //和socket通讯相关
#include "ngx_c_memory.h"      //和内存分配释放等相关
#include "ngx_c_threadpool.h"  //和多线程有关
#include "ngx_c_crc32.h"       //和crc32校验算法有关 
#include "ngx_c_slogic.h"      //和socket通讯相关

#include "library_manager.h"  // 包含业务逻辑头文件

static void freeresource();

//设置标题的全局量
size_t  g_argvneedmem=0;        //保存下这些argv参数所需要的内存大小
size_t  g_envneedmem=0;         //环境变量所占内存大小
int     g_os_argc;              //参数个数 
char    **g_os_argv;            //原始命令行参数数组,在main中会被赋值
char    *gp_envmem=NULL;        //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
int     g_daemonized=0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了

//CSocekt      g_socket;          //socket全局对象
CLogicSocket   g_socket;        //socket全局对象  
CThreadPool    g_threadpool;    //线程池全局对象

//和进程本身有关的全局量
pid_t   ngx_pid;                //当前进程的pid
pid_t   ngx_parent;             //父进程的pid
int     ngx_process;            //进程类型，比如master,worker进程等
int     g_stopEvent;            //标志程序退出,0不退出1，退出

sig_atomic_t  ngx_reap;         //标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
                                   //一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】                                   
// app/nginx.cxx  
#include <mysql/mysql.h>  

 
static struct MySQLInit {
    MySQLInit() { mysql_library_init(0, nullptr, nullptr); }
    ~MySQLInit() { mysql_library_end(); }
} mysql_init_cleanup;

//程序主入口函数
int main(int argc, char *const *argv)
{     

    int exitcode = 0;           //退出代码，先给0表示正常退出
    ngx_log_error_core(NGX_LOG_INFO, 0, "服务器启动，正在进行初始化...");
    int i;                      
    
    g_stopEvent = 0;            //标记程序是否退出，0不退出          

    ngx_pid    = getpid();      //取得进程pid
    ngx_parent = getppid();     //取得父进程的id 

    //统计argv所占的内存
    g_argvneedmem = 0;
    for(i = 0; i < argc; i++) 
    {
        g_argvneedmem += strlen(argv[i]) + 1;
    } 
    
    //统计环境变量所占的内存。判断方法是environ[i]是否为空作为环境变量结束标记
    for(i = 0; environ[i]; i++) 
    {
        g_envneedmem += strlen(environ[i]) + 1; //+1因为末尾有\0,占实际内存位置的
    } 

    g_os_argc = argc;           //保存参数个数
    g_os_argv = (char **) argv; //保存参数指针

    //全局量初始化
    ngx_log.fd = -1;                  //-1：表示日志文件尚未打开；因为后边ngx_log_stderr要用所以这里先给-1
    ngx_process = NGX_PROCESS_MASTER; //先标记本进程是master进程
    ngx_reap = 0;                     //标记子进程没有发生变化
   
    CConfig *p_config = CConfig::GetInstance(); //单例类

    ngx_log_error_core(NGX_LOG_INFO, 0, "开始加载配置文件：%s", "nginx.conf");
    
    if(p_config->Load("nginx.conf") == false) //配置文件内容载入到内存            
    {   
        ngx_log_error_core(NGX_LOG_INFO, 0, "正在初始化日志系统...");
        ngx_log_init();    //初始化日志
        ngx_log_error_core(NGX_LOG_INFO, 0, "日志系统初始化完成");
        ngx_log_stderr(0,"配置文件[%s]载入失败，退出!","nginx.conf");
        //exit(1);终止进程，exit(0)表示程序正常, exit(1)/exit(-1)表示程序异常退出，exit(2)表示表示系统找不到指定的文件
        exitcode = 2; //标记找不到文件
        goto lblexit;
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "配置文件[%s]加载成功", "nginx.conf");  

    CMemory::GetInstance();	
    ngx_log_error_core(NGX_LOG_INFO, 0, "内存管理模块初始化完成");
    //crc32校验算法单例类初始化
    CCRC32::GetInstance();
    ngx_log_error_core(NGX_LOG_INFO, 0, "CRC32校验模块初始化完成"); 
        
    //事先准备好的资源初始化
    ngx_log_error_core(NGX_LOG_INFO, 0, "正在初始化日志系统..."); 
    ngx_log_init();             //日志初始化(创建/打开日志文件)；     
    ngx_log_error_core(NGX_LOG_INFO, 0, "日志系统初始化完成");
    //(4)初始化函数
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "正在初始化信号处理...");
    if(ngx_init_signals() != 0) //信号初始化
    {
        exitcode = 1;
        goto lblexit;
    }        
    ngx_log_error_core(NGX_LOG_INFO, 0, "信号处理初始化完成"); 

    ngx_log_error_core(NGX_LOG_INFO, 0, "正在初始化监听端口..."); 
    if(g_socket.Initialize() == false)//初始化socket
    {
        ngx_log_error_core(NGX_LOG_ERR, 0, "监听端口初始化失败，退出!"); 
        exitcode = 1;
        goto lblexit;
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "监听端口初始化成功"); 

    ngx_init_setproctitle();    
    ngx_log_error_core(NGX_LOG_INFO, 0, "环境变量设置完成");

    //------------------------------------
    //创建守护进程
    ngx_log_error_core(NGX_LOG_INFO, 0, "是否启动守护进程...");
    if(p_config->GetIntDefault("Daemon",0) == 1) //读配置文件，拿到配置文件中是否按守护进程方式启动的选项
    {
        ngx_log_error_core(NGX_LOG_INFO, 0, "正在启动守护进程模式...");
        //守护进程方式运行
        int cdaemonresult = ngx_daemon();
        if(cdaemonresult == -1) //fork()失败
        {
            exitcode = 1;    //标记失败
            goto lblexit;
        }
        if(cdaemonresult == 1)
        {
            //原始父进程
            freeresource();   //只有进程退出了才goto到 lblexit，用于提醒用户进程退出了
            
            ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程创建成功，父进程退出，子进程ID: %d", getpid()); 
            exitcode = 0;
            return exitcode;  //整个进程退出
        }
        g_daemonized = 1;    //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
        ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程模式启动成功，进程ID: %d", getpid());
    }

    //// 在进入主循环之前调用业务逻辑
    //{
    //    // 创建图书馆管理系统对象
    //    LibraryManager libraryManager;

    //    // 添加一些书籍
    //    libraryManager.AddBook("The Catcher in the Rye", "J.D. Salinger");
    //    libraryManager.AddBook("To Kill a Mockingbird", "Harper Lee");

    //    // 列出所有书籍
    //    libraryManager.ListBooks();

    //    // 借出一本书
    //    libraryManager.CheckOutBook(1);

    //    // 归还一本书
    //    libraryManager.ReturnBook(1);

    //    // 移除一本书
    //    libraryManager.RemoveBook(2);

    //    // 列出所有书籍
    //    libraryManager.ListBooks();
    //}
    ngx_log_error_core(NGX_LOG_INFO, 0, "Master进程已启动，进程ID: %d", getpid()); 
    ngx_master_process_cycle(); //进程在该函数里循环；
        
lblexit:
    //释放资源
    ngx_log_stderr(0,"程序退出，再见了!");
    freeresource();  //释放动作函数
    ngx_log_error_core(NGX_LOG_INFO, 0, "程序退出，退出码: %d", exitcode); 
    return exitcode;
}

void freeresource()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始释放系统资源");
    //设置可执行程序标题导致的环境变量分配的内存释放
    if(gp_envmem)
    {
        ngx_log_error_core(NGX_LOG_INFO, 0, "释放环境变量内存");
        delete []gp_envmem;
        gp_envmem = NULL;
    }

    //关闭日志文件
    if(ngx_log.fd != STDERR_FILENO && ngx_log.fd != -1)  
    {        
        ngx_log_error_core(NGX_LOG_INFO, 0, "关闭日志文件");
        close(ngx_log.fd); 
        ngx_log.fd = -1;         
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "系统资源释放完毕");
}
