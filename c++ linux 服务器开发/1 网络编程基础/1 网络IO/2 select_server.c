#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
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
	listen(listen_fd, 5);

	// 6、初始化fd集和
	fd_set rfd;
	FD_ZERO(&rfd);
	FD_SET(listen_fd, &rfd);

	int maxfd = listen_fd;
	while(1) {
		fd_set tmp_fd = rfd;

		// 7、调用select
		int num = select(maxfd + 1, &tmp_fd, NULL, NULL, 0);
		if (num == -1) {
			printf("error select\n");
			close(listen_fd);
			break;
		} 
		
		if (num == 0) {		// 没有事件，继续
			continue;
		}

		// 先判断listen_fd是否有事件
		if (FD_ISSET(listen_fd, &tmp_fd)) {
			struct sockaddr_in client_addr;
			socklen_t len = sizeof(client_addr);

			// 8、接受连接上的客户端
			int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);
			if (client_fd == -1) {
				printf("accept error\n");
			} else {
				printf("client %d was connected!\n", client_fd);
				FD_SET(client_fd, &rfd);
				if (client_fd > maxfd) maxfd = client_fd;	// 如果新的fd大于maxfd，则替换
			}
		}

		// 下面以此检查后面的fd是否有事件
		int fd = listen_fd + 1;
		for (;fd <= maxfd;fd ++) {
			if (FD_ISSET(fd, &tmp_fd) == 0) {  // 无事件
				continue;
			} else { // 9、处理已经连接上的客户端的请求
				char buffer[1024];
				memset(buffer, 0, sizeof(buffer));
				int res = recv(fd, buffer, sizeof(buffer), 0);
				if (res == 0) { // 对方断开连接
					close(fd);
					printf("client [%d] disconnected !\n", fd);
					FD_CLR(fd, &rfd);	// 从集和中清除
				} else {
					printf("recv data from [%d] is: %s\n", fd, buffer);
					send(fd, buffer, sizeof(buffer), 0);
				}
			}
		}
	}

	return 0;
}
