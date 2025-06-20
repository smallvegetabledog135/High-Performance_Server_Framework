/*
 * @Author: smallvegetabledog135 1642165809@qq.com
 * @Date: 2025-02-16 00:55:15
 * @LastEditors: smallvegetabledog135 1642165809@qq.com
 * @LastEditTime: 2025-06-20 04:28:54
 * @FilePath: /nginx/proc/ngx_daemon.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>     //errno
#include <sys/stat.h>
#include <fcntl.h>


#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//守护进程初始化
//执行失败：返回-1，   子进程：返回0，父进程：返回1
int ngx_daemon()
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始初始化守护进程");
    //fork()一个子进程
    switch (fork())
    {
    case -1:
        //创建子进程失败
        ngx_log_error_core(NGX_LOG_EMERG, errno, "守护进程创建失败，fork()调用出错: %s", strerror(errno));
        return -1;
    case 0:
        ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程子进程创建成功，pid: %d", getpid());
        break;
    default:
        ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程父进程准备退出，pid: %d", getpid());
        return 1;  //父进程直接返回1；
    } 

    ngx_parent = ngx_pid;    
    ngx_pid = getpid();   
    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程pid更新，父进程: %d，当前进程: %d", ngx_parent, ngx_pid);

    //脱离终端，终端关闭
    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程开始脱离终端控制");
    if (setsid() == -1)  
    {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "守护进程脱离终端失败，setsid()调用出错: %s", strerror(errno));
        return -1;
    }

    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程成功脱离终端控制");  

    //设置为0，不要让它来限制文件权限 
    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程开始设置文件权限掩码为0");  
    umask(0);  
    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程文件权限掩码设置完成");

    //打开黑洞设备，以读写方式打开
    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程开始重定向标准输入输出流");
    int fd = open("/dev/null", O_RDWR);
    if (fd == -1) 
    {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "打开/dev/null设备失败: %s", strerror(errno));  
        return -1;
    }
    if (dup2(fd, STDIN_FILENO) == -1) 
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中dup2(STDIN)失败!");        
        return -1;
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "成功打开/dev/null设备，文件描述符: %d", fd);  
    
    // 重定向标准输入  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "开始重定向标准输入到/dev/null");
    if (dup2(fd, STDOUT_FILENO) == -1) 
    {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "重定向标准输入失败: %s", strerror(errno));  
        return -1;
    }
    if (fd > STDERR_FILENO)  
     {
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "准备关闭/dev/null文件描述符: %d", fd);
        if (close(fd) == -1)  //释放资源，这个文件描述符就可以被复用；不然这个数字【文件描述符】会被一直占着；
        {
            ngx_log_error_core(NGX_LOG_EMERG, errno, "关闭文件描述符失败: %s", strerror(errno));  
            return -1;  
        }
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "文件描述符关闭成功"); 
    }
    ngx_log_error_core(NGX_LOG_INFO, 0, "守护进程初始化完成");
    return 0; 
}

