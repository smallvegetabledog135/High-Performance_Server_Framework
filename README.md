# 项目概述
本项目是一个类 Nginx 架构的多进程网络服务器框架，采用 C/C++ 开发，专为高性能、高并发场景设计。框架提供了完整的网络通信、多进程管理、线程池调度和内存管理功能，并集成了一个完整的图书管理系统业务示例，展示了如何在高性能服务器上实现实际业务。
# 编译步骤
1、安装依赖  
sudo apt-get install libmysqlclient-dev  
2、编译   
make  
3、清理  
make clean
### 配置与启动
# 配置文件（nginx.conf）
1、WorkerProcesses：Worker进程数量  
2、Daemon：是否以守护进程运行  
3、ProcMsgRecvWorkThreadCount：消息处理线程数  
4、ListenPort0：监听端口  
5、worker_connections：每个Worker最大连接数  
6、Sock_RecyConnectionWaitTime：连接回收等待时间(秒)  
7、LogLevel：日志级别(0-8)  
# 启动服务器
./nginx
# 二次开发
添加新业务模块：  
1、在logic/目录下创建业务处理类  
2、继承或参考CLogicSocket类实现请求处理  
3、注册新消息类型和处理函数  
修改通信协议：  
1、在_include/ngx_c_socket.h中修改消息头定义  
2、更新ngx_c_socket_request.cpp中的消息解析逻辑  
# 目录结构
├── app/ # 核心应用层  
│ ├── nginx.cpp # 主程序入口  
│ ├── ngx_c_conf.cpp # 配置管理  
│ ├── ngx_log.cpp # 日志管理  
│ └── ... # 其他应用层代码  
├── logic/ # 业务逻辑层  
│ ├── ngx_c_slogic.cpp # 服务器逻辑  
│ └── library_manager.cpp # 图书管理业务  
├── misc/ # 通用组件  
│ ├── ngx_c_threadpool.cpp # 线程池实现  
│ ├── ngx_c_memory.cpp # 内存管理  
│ └── ngx_c_crc32.cpp # CRC32校验  
├── proc/ # 进程管理  
│ ├── ngx_daemon.cpp # 守护进程  
│ ├── ngx_process_cycle.cpp # 进程循环  
│ └── ngx_event.cpp # 事件处理  
├── signal/ # 信号处理  
│ └── ngx_signal.cpp # 信号管理  
├── socket/ # 网络通信层  
│ ├── ngx_c_socket.cpp # Socket核心类  
│ ├── ngx_c_socket_conn.cpp # 连接管理  
│ ├── ngx_c_socket_accept.cpp # 连接接受  
│ ├── ngx_c_socket_request.cpp # 请求处理  
│ ├── ngx_c_socket_time.cpp # 定时器处理  
│ └── ngx_c_socket_inet.cpp # IP/端口处理  
└── conf/ # 配置文件  
└── nginx.conf # 主配置文件  
# 请求处理流程    
![image](https://github.com/user-attachments/assets/0ed89f68-e0c7-49f6-af74-7e535b69dbf6)

