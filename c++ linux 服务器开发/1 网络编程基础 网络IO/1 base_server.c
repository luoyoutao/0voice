#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>

/*
* 功能： 线程函数，处理客户端的请求
*       这里打印接收到的信息，打印后再返回给客户端
*/
void *deal_request(void *arg) {
	int fd = (int)(long)arg;
	char buffer[1024];
	while (1) {
		memset(buffer, 0, sizeof(buffer));
		int res = recv(fd, buffer, sizeof(buffer), 0);
		if (res == 0) {		// 客户端退出连接
			close(fd);
			printf("客户端[%d]退出\n", fd);
			break;
		} else {
			printf("接收到客户端[%d]的数据是: %s\n", fd, buffer);
			send(fd, buffer, sizeof(buffer), 0);
		}
	}
	return (void*)0;
}

int main(int argc, char* args[]) {
	if (argc != 1 + 1) {
		printf("please give port of server!\n");
		return -1;
	}
	int port = atoi(args[1]);

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
	server_addr.sin_port = htons(port);

	// 4、将套接字和IP、端口绑定
	bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	
	// 5、开始监听
	listen(listen_fd, 5);

	while (1) {
		// 6、接受连接上的客户端
		struct sockaddr_in client_addr;
		socklen_t len = sizeof(client_addr);
		int connet_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);

		printf("客户端[%d]连接上\n", connet_fd);

		// 7、创建一个线程来处理当前客户端的请求
		pthread_t pid;
		pthread_create(&pid, NULL, deal_request, (void*)(long)connet_fd);
	}
	return 0;
}
