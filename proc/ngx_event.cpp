/*
 * @Author: smallvegetabledog135 1642165809@qq.com
 * @Date: 2025-02-16 23:45:19
 * @LastEditors: smallvegetabledog135 1642165809@qq.com
 * @LastEditTime: 2025-06-19 20:56:41
 * @FilePath: /nginx/proc/ngx_event.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//开启子进程相关

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   
#include <errno.h>  
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//处理网络事件和定时器事件
void ngx_process_events_and_timers()

{
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始处理网络事件和定时器事件");
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "等待epoll事件，超时时间：%d毫秒", -1);    
    int events_processed = g_socket.ngx_epoll_process_events(-1);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "epoll事件处理完毕，共处理 %d 个事件", events_processed);  
    
    //统计信息打印
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "开始打印统计信息");  
    g_socket.printTDInfo();
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "统计信息打印完毕");
    ngx_log_error_core(NGX_LOG_INFO, 0, "网络事件和定时器事件处理完毕");
}

