#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <string.h>

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

	// 6、声明和初始化struct pollfd类型数组
	int maxfd = 1023;
	struct pollfd fds[maxfd + 1];
	int i = 0;
	for(;i < maxfd + 1;i ++) {
		fds[i].fd = -1;
	}

	// 7、将listen_fd放入首位置并监听
	fds[0].fd = listen_fd;
	fds[0].events = POLLIN;

	while (1) {
		int num = poll(fds, maxfd + 1, 50);
		if (num == -1) {
			printf("poll error\n");
			close(listen_fd);
			break;
		} else if (num == 0) {
			continue;
		} else {
			// 先判断是否有新的客户端连接进来
			if(fds[0].revents & POLLIN) {
				struct sockaddr_in client_addr;
				socklen_t len = sizeof(client_addr);
				int connet_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);
				printf("client [%d] is connected!\n", connet_fd);
				int i = 1;
				for(;i < maxfd + 1;i ++) {		// 找个空位置把新的客户端监听起来
					if (fds[i].fd == -1) {
						fds[i].fd = connet_fd;
						fds[i].events = POLLIN;
						break;
					}
				}
			}

			// 判断其他的df是否有事件
			int i = 1;
			for(;i < maxfd + 1;i ++) {
				if(fds[i].fd == -1) {
					continue;
				}
				if(fds[i].revents & POLLIN) {
					char buffer[1024];
					memset(buffer, 0, sizeof(buffer));
					int res = recv(fds[i].fd, buffer, sizeof(buffer), 0);
					if(res == 0) {
						close(fds[i].fd);
						printf("client [%d] is disconnected!\n", fds[i].fd);
						fds[i].fd = -1;
					} else {
						printf("recv data from client [%d] is %s\n", fds[i].fd, buffer);
						send(fds[i].fd, buffer, sizeof(buffer), 0);
					}
				}
			}

		}
	}

	return 0;
}
