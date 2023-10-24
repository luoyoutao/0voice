#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *args[]) {
	if (argc != 1 + 1) {
		printf("please give port!\n");
		return 0;
	}

	// 1、创建套接字
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

	// 2、设置端口复用
	int opt = 1;
	unsigned int len = sizeof(opt);
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, len);

	// 3、设置服务端的IP和端口
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(atoi(args[1]));

	// 4、将套接字和IP、端口绑定
	bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

	// 5、开始监听
	listen(listen_fd, 50);

	// 6、创建epoll实例
	int epfd = epoll_create1(0);

	// 7、注册listen_fd和事件
	struct epoll_event event;
	event.data.fd = listen_fd;
	event.events = EPOLLIN | EPOLLET;  // 可读事件 | 边缘触发
	epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &event);

	// 8、申请epoll_event数组
	int maxfd = 1024;
	struct epoll_event events[maxfd];
	
	while (1) {
		// 9、循环中调用epoll_wait来等待事件发生
		int num = epoll_wait(epfd, events, maxfd, 20);
		int i = 0;
		for (;i < num;i ++) {
			if (listen_fd == events[i].data.fd) {  // listen的socket有事件
				struct sockaddr_in client_addr;
				socklen_t len = sizeof(client_addr);
				int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);
				if (conn_fd == -1) {
					perror("accept error.");
					continue;
				} else {
					printf("client [%d] is connected !\n", conn_fd);
					struct epoll_event event_tmp;
					event_tmp.data.fd = conn_fd;
					event_tmp.events = EPOLLIN | EPOLLET;
					epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &event_tmp);
				}
			} else {	// 已经连接的客户端socket有事件
		                int client_fd = events[i].data.fd;
				char buffer[1024];
				memset(buffer, 0, sizeof(buffer));
				int res = recv(client_fd, buffer, sizeof(buffer), 0); 
				if (res == 0) {
					close(client_fd);
					printf("client [%d] is disconnected !\n", client_fd);
				} else {
					printf("recv data from client [%d] is %s\n", client_fd, buffer);
					send(client_fd, buffer, sizeof(buffer), 0);
		                }
	            	}
		}
	}

	return 0;
}
