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

//建立新连接函数，当新连接进入时，本函数会被ngx_epoll_process_events()所调用
void CSocekt::ngx_event_accept(lpngx_connection_t oldc)
{
    struct sockaddr    mysockaddr;        //远端服务器的socket地址
    socklen_t          socklen;
    int                err;
    int                level;
    int                s;
    static int         use_accept4 = 1;   
    lpngx_connection_t newc;              //连接池中的一个连接
    
    ngx_log_error_core(NGX_LOG_INFO, 0, "开始接受新连接...");

    socklen = sizeof(mysockaddr);
    do   
    {     
        if(use_accept4)
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "使用accept4()接受新连接");
            //listen套接字是非阻塞的，所以即便已完成连接队列为空，accept4()也不会卡在这里；
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK); 
        }
        else
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "使用accept()接受新连接");
            //listen套接字是非阻塞的，所以即便已完成连接队列为空，accept()也不会卡在这里；
            s = accept(oldc->fd, &mysockaddr, &socklen);
        }


        if(s == -1)
        {
            err = errno;
            if(err == EAGAIN) 
            {
                ngx_log_error_core(NGX_LOG_DEBUG, 0, "accept4()/accept()返回EAGAIN/EWOULDBLOCK，暂无新连接");  
                return; 
            } 
            level = NGX_LOG_ALERT;
            if (err == ECONNABORTED)  //对方意外关闭套接字后
            {
                level = NGX_LOG_ERR;
                ngx_log_error_core(level, err, "接受连接时发生ECONNABORTED错误，客户端可能异常断开");  
            } 
            else if (err == EMFILE || err == ENFILE) //EMFILE:进程的fd已用尽。                                   
            {
                level = NGX_LOG_CRIT;
            }
            if(use_accept4 && err == ENOSYS) //accept4()函数没实现
            {
                ngx_log_error_core(NGX_LOG_WARN, 0, "accept4()函数不被系统支持，切换到accept()");
                use_accept4 = 0; 
                continue;         //重新用accept()函数
            }

            if (err == ECONNABORTED)  //对方关闭套接字
            {
                //do nothing
            }
            
            if (err == EMFILE || err == ENFILE) 
            {
                //do nothing
            }            
            return;
        } 

        // 连接成功后，检查连接数限制  
        struct sockaddr_in* sin = (struct sockaddr_in*)&mysockaddr;  
        char clientIP[100];  
        inet_ntop(AF_INET, &sin->sin_addr, clientIP, sizeof(clientIP));  
        
        ngx_log_error_core(NGX_LOG_INFO, 0, "接受新连接成功，客户端IP: %s:%d，socket fd: %d",   
                          clientIP, ntohs(sin->sin_port), s);

        //accept4()/accept()成功        
        if(m_onlineUserCount >= m_worker_connections)  //用户连接数过多，要关闭该用户socket，直接关闭
        {
            ngx_log_error_core(NGX_LOG_WARN, 0, "超出系统允许的最大连接数(%d)，关闭新连接(fd=%d, IP=%s)",  
                              m_worker_connections, s, clientIP);
            return ;
        }
        //如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用ngx_get_connection()使用,短时间内产生大量连接，危及本服务器安全
        if(m_connectionList.size() > (m_worker_connections * 5))
        {
            if(m_freeconnectionList.size() < m_worker_connections)
            {
                ngx_log_error_core(NGX_LOG_WARN, 0, "连接池异常(%zu/%zu)，可能遭受攻击，拒绝新连接(fd=%d, IP=%s)",  
                                  m_freeconnectionList.size(), m_connectionList.size(), s, clientIP);  
                close(s);
                return ;   
            }
        }

        ngx_log_error_core(NGX_LOG_DEBUG, 0, "从连接池获取连接对象，fd=%d", s); 
        newc = ngx_get_connection(s); //针对新连入用户的连接
        if(newc == NULL)
        {
            ngx_log_error_core(NGX_LOG_ERR, 0, "连接池已满，无法获取连接对象，关闭socket(fd=%d)", s);
            //连接池中连接不够用，把这个socekt直接关闭并返回
            if(close(s) == -1)
            {
                ngx_log_error_core(NGX_LOG_ALERT, errno, "关闭socket失败(fd=%d)", s);                
            }
            return;
        }

        //成功的拿到了连接池中的一个连接
        memcpy(&newc->s_sockaddr,&mysockaddr,socklen);  //拷贝客户端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】

        if(!use_accept4)
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "设置socket为非阻塞模式(fd=%d)", s);
            //如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
            if(setnonblocking(s) == false)
            {
                ngx_log_error_core(NGX_LOG_ERR, 0, "设置socket非阻塞模式失败(fd=%d)", s);
                //设置非阻塞失败
                ngx_close_connection(newc); //关闭socket
                return; //直接返回
            }
        }

        newc->listening = oldc->listening;                    //连接对象 和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】           
        
        newc->rhandler = &CSocekt::ngx_read_request_handler;  //设置数据来时的读处理函数
        newc->whandler = &CSocekt::ngx_write_request_handler; //设置数据发送时的写处理函数。
        
        ngx_log_error_core(NGX_LOG_DEBUG, 0, "将新连接加入epoll监控(fd=%d)", s);

        //客户端应该主动发送第一次的数据，这里将读事件加入epoll监控，这样当客户端发送数据来时，会触发ngx_wait_request_handler()被ngx_epoll_process_events()调用        
        if(ngx_epoll_oper_event(
                                s,                  //socekt句柄
                                EPOLL_CTL_ADD,      //事件类型，这里是增加
                                EPOLLIN|EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭 ，如果边缘触发模式可以增加 EPOLLET
                                0,                  
                                newc                //连接池中的连接
                                ) == -1)         
        {
            ngx_log_error_core(NGX_LOG_ERR, errno, "将新连接加入epoll监控失败(fd=%d)", s);
            //增加事件失败
            ngx_close_connection(newc);//关闭socket
            return; 
        }

        if(m_ifkickTimeCount == 1)
        {
            ngx_log_error_core(NGX_LOG_DEBUG, 0, "将连接添加到超时检查队列(fd=%d)", s);
            AddToTimerQueue(newc);
        }
        ++m_onlineUserCount;  //连入用户数量+1    
        ngx_log_error_core(NGX_LOG_INFO, 0, "连接已建立，当前在线用户数: %d", m_onlineUserCount.load());    
        break;
    } while (1);   

    return;
}

