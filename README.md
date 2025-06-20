# 项目概述
本项目是一个类 Nginx 架构的多进程网络服务器框架，采用 C/C++ 开发，专为高性能、高并发场景设计。框架提供了完整的网络通信、多进程管理、线程池调度和内存管理功能，并集成了一个完整的图书管理系统业务示例，展示了如何在高性能服务器上实现实际业务。
# 编译步骤
1、安装依赖  
sudo apt-get install libmysqlclient-dev  
2、编译   
make  
3、清理  
make clean
# 启动  
./nginx
# 二次开发
添加新业务模块：  
1、在logic/目录下创建业务处理类  
2、继承或参考CLogicSocket类实现请求处理  
3、注册新消息类型和处理函数  
# 修改通信协议
步骤：  
1、在_include/ngx_c_socket.h中修改消息头定义  
2、更新ngx_c_socket_request.cpp中的消息解析逻辑  
# 架构设计
