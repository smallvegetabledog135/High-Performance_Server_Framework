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
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

//参数sa：客户端的ip地址信息。
//参数port：为1，表示要把端口信息也放到组合成的字符串里，为0，不包含端口信息
//参数text：文本
//参数len：文本的宽度
size_t CSocekt::ngx_sock_ntop(struct sockaddr *sa,int port,u_char *text,size_t len)
{
    struct sockaddr_in   *sin;
    u_char               *p;

    switch (sa->sa_family)
    {
    case AF_INET:
        sin = (struct sockaddr_in *) sa;
        p = (u_char *) &sin->sin_addr;
        if(port)  //端口信息组合到字符串里
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud:%d", p[0], p[1], p[2], p[3], ntohs(sin->sin_port));  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "IPv4地址转换(含端口): %ud.%ud.%ud.%ud:%d",   
                              p[0], p[1], p[2], p[3], ntohs(sin->sin_port));  
        }
        else //不需要组合端口信息到字符串中
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud", p[0], p[1], p[2], p[3]);  
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "IPv4地址转换(不含端口): %ud.%ud.%ud.%ud",   
                              p[0], p[1], p[2], p[3]);  
        }
        return (p - text);
        break;
    default:
        ngx_log_error_core(NGX_LOG_WARN, 0, "ngx_sock_ntop()遇到未知地址族: %d", sa->sa_family);  
        
        return 0;
        break;
    }
    return 0;
}
