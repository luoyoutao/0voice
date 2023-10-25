#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/sendfile.h>

#define BUFFER_LENGTH		1024
#define MAX_EPOLL_EVENTS	1024
#define SERVER_PORT			8888
#define PORT_COUNT 			10

#define HTTP_METHOD_GET 0
#define HTTP_METHOD_POST 1
#define HTTP_WEBSERVER_HTML_ROOT "html"

typedef int (*NCALLBACK)(int ,int, void*);

struct event {
	int fd;
	int events;
	void *arg;
	int (*callback)(int fd, int events, void *arg);
	
	int status;
	char buffer[BUFFER_LENGTH];

	int length;
	long last_active;

	// http param
	int method;
	char resource[BUFFER_LENGTH];
};

struct eventblock{
	struct event *events;
	struct eventblock *next;
};

struct reactor {
	int epfd;
	int blkcnt;
	struct eventblock *evblk;	// --> 100W
};


int recv_cb(int fd, int events, void *arg);
int send_cb(int fd, int events, void *arg);


struct event * find_event_byfd(struct reactor* r, int fd) {
	int blkid = fd / MAX_EPOLL_EVENTS;	//	计算出在第几块
	while (blkid >= r->blkcnt) {
		reactor_alloc(r);
	}

	// 查找到该块的位置
	int i = 0;
	struct eventblock *blk = r->evblk;
	while (i < blkid && blk != NULL) {
		blk = blk->next;
		i ++;
	}

	return &blk->events[fd % MAX_EPOLL_EVENTS];
}


void event_set(struct event *ev, int fd, NCALLBACK callback, void *arg) {
	ev->fd = fd;
	ev->callback = callback;
	ev->events = 0;
	ev->arg = arg;
	ev->last_active = time(NULL);
}


int event_add(int epfd, int events, struct event *ev) {
	struct epoll_event ep_ev = {0, {0}};
	ep_ev.data.ptr = ev;
	ep_ev.events = ev->events = events;

	int op;
	if (ev->status == 1) {	// 设置操作为MOD
		op = EPOLL_CTL_MOD;
	} else {	// 首次设置操作为ADD
		op = EPOLL_CTL_ADD;
		ev->status = 1;
	}

	if (epoll_ctl(epfd, op, ev->fd, &ep_ev) < 0) {
		printf("event add or mod failed [fd=%d], events[%d]\n", ev->fd, events);
		return -1;
	}

	return 0;
}


int event_del(int epfd, struct event *ev) {
	struct epoll_event ep_ev = {0, {0}};

	if (ev->status != 1) {
		return -1;
	}

	ep_ev.data.ptr = ev;
	ev->status = 0;
	epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, &ep_ev);

	return 0;
}


int recv_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = recv(fd, ev->buffer, BUFFER_LENGTH, 0);

	if (len > 0) {
		ev->length = len;
		ev->buffer[len] = '\0';
		printf("C[%d]:%s\n", fd, ev->buffer);

		http_request(ev);

		event_del(r->epfd, ev);
		event_set(ev, fd, send_cb, r);
		event_add(r->epfd, EPOLLOUT, ev);
	} else if (len == 0) {
		close(ev->fd);
		printf("[fd=%d], closed\n", fd);
	} else {
		close(ev->fd);
		printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
	}

	return len;
}


int http_response (struct event *ev) {
	if (ev == NULL) return -1;
	memset(ev->buffer, 0, BUFFER_LENGTH);

	const char* body = "<html><head><title>hello http</title></head><body><H1>http</H1></body></html>\0";

	ev->length = sprintf(ev->buffer, 
		"HTTP/1.1 200 OK\r\n\
		 Date: Sun, 24 Sep 2023 02:23:56 GMT\r\n\
		 Content-Type: text/html;charset=ISO-8859-1\r\n\
		 Content-Length: %d\r\n\r\n%s",
		 strlen(body) + 1, body);

	return ev->length;
}

/*
allbuf是所有buf
idx是从哪里的位置开读一行
linbuf是读取到的一行信息
*/
int readline(const char *allbuf, int idx, char *linebuf) {
	int len = strlen(allbuf);

	for(; idx < len; idx ++) {
		if(allbuf[idx] == '\r' && allbuf[idx + 1] == '\n') {
			return idx + 2;	//	下次从这个位置开始读取，即下一行的开始位置
		} else {
			*(linebuf ++) = allbuf[idx];	// 一个字节一个字节往linebuf里面存储
		}
	}
}


int http_request(struct event *ev) {
	char linebuf[1024];
	int idx = readline(ev->buffer, 0, linebuf);

	if (strstr(linebuf, "GET") || strstr(linebuf, "get")) {
		ev->method = HTTP_METHOD_GET;
		
		// 找出get请求的url资源 
		int i  =0;
		while(linebuf[sizeof("GET ") + i]  != ' ') i ++;
		linebuf[sizeof("GET ") + i] = '\0';
		sprintf(ev->resource, "./%s/%s", HTTP_WEBSERVER_HTML_ROOT, linebuf + sizeof("GET "));

		// 查看资源文件是否存在
		printf("正在查找资源 %s 中....\n", ev->resource);
		int filefd = open(ev->resource, O_RDONLY);
		if (filefd == -1) {	// 资源文件不存在
			printf("资源 %s 不存在 !!!\n", ev->resource);
		} else { // 资源文件存在
			printf("资源 %s 存在，正在处理中....\n", ev->resource);
			// 获取文件长度
			struct stat stat_buf;
			fstat(filefd, &stat_buf);
			close(filefd);

			// 
			ev->length = sprintf(ev->buffer, 
				"HTTP/1.1 200 OK\r\n\
				 Date: Sun, 24 Sep 2023 02:23:56 GMT\r\n\
				 Content-Type: text/html;charset=ISO-8859-1\r\n\
				 Content-Length: %d\r\n\r\n",
				 stat_buf.st_size);
		}
	} else if(strstr(linebuf, "POST") || strstr(linebuf, "post")) {
		ev->method = HTTP_METHOD_POST;
	}
}


int send_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	http_response(ev);

	int len = send(fd, ev->buffer, ev->length, 0);
	if (len > 0) {
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

		int filefd = open(ev->resource, O_RDONLY);
		struct stat stat_buf;
		fstat(filefd, &stat_buf);

		sendfile(fd, filefd, NULL, stat_buf.st_size);	// 发送文件
		close(filefd);

		event_del(r->epfd, ev);
		event_set(ev, fd, recv_cb, r);
		event_add(r->epfd, EPOLLIN, ev);
	} else {
		close(ev->fd);
		event_del(r->epfd, ev);
		printf("send[fd=%d] error %s\n", fd, strerror(errno));
	}

	return len;
}


int accept_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	if (r == NULL) return -1;

	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);

	int clientfd;
	if ((clientfd = accept(fd, (struct sockaddr*)&client_addr, &len)) == -1) {
		if (errno != EAGAIN && errno != EINTR) {
		}
		printf("accept: %s\n", strerror(errno));
		return -1;
	}

	// 设置 clientfd 为非阻塞
	if (fcntl(clientfd, F_SETFL, O_NONBLOCK) < 0) {
		printf("%s: fcntl nonblocking failed\n", __func__);
		return -1;
	}

	// 根据刚接受的clientfd找到其event应该在events线性表中的具体位置
	struct event * ev = find_event_byfd(r, clientfd);

	// 然后设置clientfd的监听事件
	event_set(ev, clientfd, recv_cb, r);
	event_add(r->epfd, EPOLLIN, ev);

	printf("new connect [%s:%d], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), clientfd);

	return 0;
}


int reactor_alloc(struct reactor *r) {
	if (r == NULL) return -1;
	if (r->evblk == NULL) return -1;

	struct eventblock *blk = r->evblk;
	while (blk->next != NULL) {
		blk = blk->next;
	}
	struct event *evs = (struct event *)malloc(MAX_EPOLL_EVENTS * sizeof(struct event));
	if (evs == NULL) {
		printf("reactor_alloc event failed\n");
		return -2;
	}
	memset(evs, 0, (MAX_EPOLL_EVENTS) * sizeof(struct event));

	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (block == NULL) {
		printf("reactor_alloc eventblock failed\n");
		return -2;
	}
	memset(block, 0, sizeof(struct eventblock));

	block->next = NULL;
	block->events = evs;

	blk->next = block;
	r->blkcnt ++;	// 记录的内存块数量加1

	return 0;
}


int reactor_destory(struct reactor *r) {
	close(r->epfd);
	
	struct eventblock *blk = r->evblk;
	struct eventblock *blk_next = NULL;
	
	// 释放所有事件块
	while(blk != NULL) {
		blk_next = blk;

		free(blk->events);
		free(blk);

		blk = blk_next;
	}

	return 0;
}


int reactor_addlistener(struct reactor *r, int sockfd, NCALLBACK acceptor) {
	if (r == NULL) return -1;
	if (r->evblk == NULL) return -1;

	// 先找到sockfd应该存储在哪个位置
	struct event *event = find_event_byfd(r, sockfd);

	event_set(event, sockfd, acceptor, r);
	event_add(r->epfd, EPOLLIN, event);

	return 0;
}


int reactor_run(struct reactor *r) {
	if (r == NULL) return -1;
	if (r->epfd < 0) return -1;
	if (r->evblk == NULL) return -1;
	
	struct epoll_event events[MAX_EPOLL_EVENTS + 1];	// 注意要 + 1

	int i;
	while (1) {
		// 每次最多从上百万的响应事件中取出 MAX_EPOLL_EVENTS + 1个
		int nready = epoll_wait(r->epfd, events, MAX_EPOLL_EVENTS, 1000);
		if (nready < 0) {
			printf("epoll_wait error, exit\n");
			continue;
		}

		for (i = 0;i < nready;i ++) {
			struct event *ev = (struct event*)events[i].data.ptr;
			
			// 如果当前通过epoll_wait返回的事件和我们要监听的事件相同
			// 则通过回调函数去处理
			if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {
				ev->callback(ev->fd, events[i].events, ev->arg);
			}
			if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {
				ev->callback(ev->fd, events[i].events, ev->arg);
			}	
		}

	}
}


int init_sock(short port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(fd, F_SETFL, O_NONBLOCK);		// 设置为非阻塞

	int optval = 1; // 允许多个套接字在同一端口上绑定
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	int ret_bind = bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

	if (listen(fd, 20) < 0) {
		printf("listen failed : %s\n", strerror(errno));
	}

	return fd;
}


int reactor_init(struct reactor *r) {
	if (r == NULL) return -1;
	memset(r, 0, sizeof(struct reactor));

	r->epfd = epoll_create(1);
	if (r->epfd <= 0) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		return -2;
	}

	// 初始化第一个 event 线性表
	struct event *evs = (struct event *)malloc((MAX_EPOLL_EVENTS) * sizeof(struct event));
	if (evs == NULL) {
		printf("reactor_alloc event failed\n");
		return -2;
	}
	memset(evs, 0, (MAX_EPOLL_EVENTS) * sizeof(struct event));

	// 初始化第一个 eventblock 块
	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (block == NULL) {
		printf("reactor_alloc eventblock failed\n");
		return -2;
	}
	memset(block, 0, sizeof(struct eventblock));
	
	block->next = NULL;
	block->events = evs;

	r->evblk = block;
	r->blkcnt = 1;

	return 0;
}


int main(int argc, char *args[]) {
	unsigned short port = SERVER_PORT;
	if (argc == 1 + 1) {
		port = atoi(args[1]);
	}

	// 初始化reactor
	struct reactor *r = (struct reactor*)malloc(sizeof(struct reactor));
	reactor_init(r);

	/*
		五元组：
		目的IP、目的端口、源IP、源端口、TCP
		使用三个虚拟机作为客户端
		由于端口最多65535，约等于6万
		3 * 6万 = 18万 < 100万
		因此，服务端多开些端口
	*/

	int i = 0;
	int sock_fds[PORT_COUNT] = {0};
	for(; i < PORT_COUNT; i ++) {
		sock_fds[i] = init_sock(port + i);		// 端口从8888开始，一直到8987
		reactor_addlistener(r, sock_fds[i], accept_cb);		// 添加监听的sockfd和事件
	}

	// 循环接受 和 处理 客户端的请求
	reactor_run(r);

	// 释放reactor中指向的各个块
	reactor_destory(r);

	// 关闭所有服务端socket
	for(; i < PORT_COUNT; i ++) {
		close(sock_fds[i]);
	}
	
	// 释放reactor
	free(r);

	return 0;
}
