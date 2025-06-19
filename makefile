include config.mk  

# 定义目标文件  
#OBJS = app/link_obj/ngx_c_socket_conn.o app/link_obj/ngx_c_socket_time.o app/link_obj/ngx_c_threadpool.o app/link_obj/ngx_log.o app/link_obj/ngx_printf.o app/link_obj/ngx_string.o app/link_obj/ngx_process_cycle.o app/link_obj/ngx_c_conf.o app/link_obj/nginx.o app/link_obj/ngx_daemon.o app/link_obj/ngx_signal.o app/link_obj/ngx_event.o app/link_obj/ngx_c_socket_accept.o app/link_obj/ngx_c_slogic.o app/link_obj/ngx_c_crc32.o app/link_obj/ngx_c_socket_request.o app/link_obj/ngx_c_socket_inet.o app/link_obj/ngx_c_memory.o app/link_obj/ngx_c_socket.o app/link_obj/ngx_setproctitle.o app/link_obj/logic/library_manager.o  
OBJS = app/link_obj/ngx_c_socket_conn.o app/link_obj/ngx_c_socket_time.o app/link_obj/ngx_c_threadpool.o app/link_obj/ngx_log.o app/link_obj/ngx_printf.o app/link_obj/ngx_string.o app/link_obj/ngx_process_cycle.o app/link_obj/ngx_c_conf.o app/link_obj/nginx.o app/link_obj/ngx_daemon.o app/link_obj/ngx_signal.o app/link_obj/ngx_event.o app/link_obj/ngx_c_socket_accept.o app/link_obj/ngx_c_slogic.o app/link_obj/ngx_c_crc32.o app/link_obj/ngx_c_socket_request.o app/link_obj/ngx_c_socket_inet.o app/link_obj/ngx_c_memory.o app/link_obj/ngx_c_socket.o app/link_obj/ngx_setproctitle.o app/link_obj/logic/library_manager.o
# 定义目标文件夹  
BUILD_DIR = signal proc net misc logic app  

# 确保创建所有必要的目录  
$(shell mkdir -p app/link_obj/logic)  

all: $(OBJS)  
	@for dir in $(BUILD_DIR); \
	do \
		make -C $$dir; \
	done  
	# 链接目标文件生成可执行文件  
	g++ -std=c++11 -g -o nginx $(OBJS) -lpthread -lmysqlclient  

# 编译规则 - 修改这部分  
app/link_obj/%.o: */%.cpp  
	g++ -std=c++11 -g -I/home/yzq/Learn_EpollServer-main/Learn_EpollServer-main/nginx/_include -o $@ -c $<  

app/link_obj/logic/%.o: logic/%.cpp  
	g++ -std=c++11 -g -I/home/yzq/Learn_EpollServer-main/Learn_EpollServer-main/nginx/_include -o $@ -c $<  

clean:  
	rm -rf app/link_obj app/dep nginx  
	rm -rf signal/*.gch app/*.gch