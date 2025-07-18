# 项目概述
本项目是一个类 Nginx 架构的多进程网络服务器框架，采用 C/C++ 开发，专为高性能、高并发场景设计。框架提供了完整的网络通信、多进程管理、线程池调度和内存管理功能，并集成了一个完整的图书管理系统业务示例，展示了如何在高性能服务器上实现实际业务。
# 编译步骤
1、安装依赖 
```
sudo apt-get install libmysqlclient-dev
```
2、清理
```
make clean
```
3、编译   
```
make
```  

# 配置与启动
# 配置文件（nginx.conf）
1、WorkerProcesses：Worker进程数量  
2、Daemon：是否以守护进程运行  
3、ProcMsgRecvWorkThreadCount：消息处理线程数  
4、ListenPort0：监听端口  
5、worker_connections：每个Worker最大连接数  
6、Sock_RecyConnectionWaitTime：连接回收等待时间(秒)  
7、LogLevel：日志级别(0-8)  
# 启动服务器
```
./nginx
```
# 二次开发
添加新业务模块：  
1、在`logic/`目录下创建业务处理类  
2、继承或参考`CLogicSocket`类实现请求处理  
3、注册新消息类型和处理函数  
修改通信协议：  
1、在`_include/ngx_c_socket.h`中修改消息头定义  
2、更新`ngx_c_socket_request.cpp`中的消息解析逻辑  
# 目录结构
```
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
```
# 请求处理流程    
![image](https://github.com/user-attachments/assets/0ed89f68-e0c7-49f6-af74-7e535b69dbf6)
# 分析与优化
- 给服务器压测时，使用`valgrind`进行性能分析
- <img width="1376" height="819" alt="image" src="https://github.com/user-attachments/assets/1acfe91c-af05-42da-a818-32c043e60aaa" />
- 结果可以看到，占用内存引用次数最多的函数`_dl_lookup_symbol_x`、`do_lookup_x`、`CCRC32::Reflect`、`CConfig::Load`主要与动态库加载和crc32校验算法相关。
- 通过`KCachegrind`看调用关系
 - <img width="486" height="514" alt="image" src="https://github.com/user-attachments/assets/7b31b064-f427-40b2-8e28-b24646ef3c46" />
- 优化方向：  
   i. 首先优化自定义函数，所以先优化crc32校验算法。  
   ii. 由于多个动态链接相关的函数耗时较高，考虑是否能够将某些库静态链接，以减少每次调用时的耗时。
## 注意事项
- 为方便调试，我加了大量的日志，但是因此导致性能下降，可以根据不同情况修改配置文件中的打印日志级别。
- 因为是自定义协议，所以`wrk`是不能用来测性能的，如果用`python`脚本的话，需要注意的是，如果是主机访问虚拟机，应该使用虚拟机的实际IP地址，不能用127.0.0.1，可以用ifconfig查看虚拟机实际IP。
- 与服务器通信时，要注意包体根据不同业务设置，包头长度需为 8 字节，结构如下：  
   i. `pkgLen`（2 字节）：整个包的长度，包括包头和包体。  
   ii. `msgCode`（2 字节）：消息代码，标识包的类型（如注册、心跳等）。  
   iii. `crc32`（4 字节）：包体的 CRC32 校验值，用于数据完整性校验。
