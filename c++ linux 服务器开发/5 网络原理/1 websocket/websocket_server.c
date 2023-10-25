#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define BUFFER_LENGTH		1024
#define MAX_EPOLL_EVENTS	1024
#define SERVER_PORT			8888
#define PORT_COUNT 1


#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

enum {
	WS_HANDSHARK = 0,
	WS_TRANMISSION = 1,
	WS_END = 2
};

typedef struct _ws_ophdr{
	unsigned char opcode:4,
						rsv3:1,
						rsv2:1,
						rsv1:1,
						fin:1;
	unsigned char pl_len:7,
						mask:1;			
} ws_ophdr;

typedef struct _ws_head_126 {
	unsigned short payload_length;
	char mask_key[4];
} ws_head_126;

typedef struct _ws_head_127 {
	long long payload_length;
	char mask_key[4];
} ws_head_127;

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

	int status_machine;		// 状态机
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

	return &blk->events[blkid % MAX_EPOLL_EVENTS];
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


int base64_encode(char *in_str, int in_len, char *out_str) {    
	BIO *b64, *bio;    
	BUF_MEM *bptr = NULL;    
	size_t size = 0;    

	if (in_str == NULL || out_str == NULL)        
		return -1;    

	b64 = BIO_new(BIO_f_base64());    
	bio = BIO_new(BIO_s_mem());    
	bio = BIO_push(b64, bio);
	
	BIO_write(bio, in_str, in_len);    
	BIO_flush(bio);    

	BIO_get_mem_ptr(bio, &bptr);    
	memcpy(out_str, bptr->data, bptr->length);    
	out_str[bptr->length-1] = '\0';    
	size = bptr->length;    

	BIO_free_all(bio);    
	return size;
}


int readline(char *allbuf, int idx, char *linebuf) {
	int len = strlen(allbuf);
	for(; idx < len; idx ++) {
		if (allbuf[idx] == '\r' && allbuf[idx+1] == '\n') {
			return idx + 2;
		} else {
			*(linebuf ++) = allbuf[idx];
		}
	}

	return -1;
}

#define WEBSOCKET_KEY_LENGTH 19		// Sec-WebSocket-Key:  固定格式，其长度
int handshark(struct event *ev) {
	char linebuf[1024] = {0};
	int idx = 0;
	char sec_data[128] = {0};
	char sec_accept[32] = {0};
	do {
		memset(linebuf, 0, 1024);
		idx = readline(ev->buffer, idx, linebuf);
		if (strstr(linebuf, "Sec-WebSocket-Key")) {
			strcat(linebuf, GUID);
			SHA1(linebuf + WEBSOCKET_KEY_LENGTH, strlen(linebuf + WEBSOCKET_KEY_LENGTH), sec_data);	// openssl
			base64_encode(sec_data, strlen(sec_data), sec_accept);

			memset(ev->buffer, 0, BUFFER_LENGTH); 
			ev->length = sprintf(ev->buffer,
				"HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: websocket\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n\r\n",
				sec_accept);

			printf("ws response : %s\n", ev->buffer);
			break;
		}
	} while ((ev->buffer[idx] != '\r' || ev->buffer[idx+1] != '\n') && idx != -1 );

	return 0;
}

/*
传送前打码
payload[length]
mask_key[4]
payload[i] = payload[i] ^ mask_key[i%4]
*/

// 解码
void umask(char *payload, int length, char *mask_key) {
	int i = 0;
	for (; i < length; i ++) {
		payload[i] ^= mask_key[i % 4];
	}
}

// 解析收到客户端的websocket格式的数据
int transmission(struct event *ev) {
	ws_ophdr *hdr = (ws_ophdr *) ev->buffer;
	printf("length: %d\n", hdr->pl_len);
	if (hdr->pl_len < 126) {
		unsigned char *payload = ev->buffer + sizeof(ws_ophdr) + 4;	// 4 = sizeof(mask_key)
		if (hdr->mask) {	// mask set 1
			umask(payload, hdr->pl_len, ev->buffer + 2);	// 2 = sizeof(ws_ophdr)
		} 
		printf("payload: %s\n", payload);
	} else if(hdr->pl_len == 126) {
		ws_head_126 *hdr126 =  (ws_head_126 *) ev->buffer + sizeof(ws_ophdr);
	} else {
		ws_head_127 *hdr127 =  (ws_head_127 *) ev->buffer + sizeof(ws_ophdr);
	}

	return 0;
}


int websocket_request(struct event *ev) {
	if (ev->status_machine == WS_HANDSHARK) {
		ev->status_machine = WS_TRANMISSION;
		handshark(ev);
	} else if(ev->status_machine == WS_TRANMISSION) {
		transmission(ev);
	} else {

	}
}


int recv_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = recv(fd, ev->buffer, BUFFER_LENGTH, 0);

	if (len > 0) {
		ev->length = len;
		ev->buffer[len] = '\0';

		websocket_request(ev);

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


int send_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = send(fd, ev->buffer, ev->length, 0);
	if (len > 0) {
		// printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

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

// 回调函数 接受已经连接的客户端的请求
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

	// 根据fd找到event应该在events线性表中的具体位置
	struct event * ev = find_event_byfd(r, fd);

	ev->status_machine = WS_HANDSHARK;
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
	struct event *ev = (struct event *)malloc(MAX_EPOLL_EVENTS * sizeof(struct event));
	if (ev == NULL) {
		printf("reactor_alloc event failed\n");
		return -2;
	}
	memset(ev, 0, MAX_EPOLL_EVENTS * sizeof(struct event));

	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (ev == NULL) {
		printf("reactor_alloc eventblock failed\n");
		return -2;
	}
	memset(block, 0, sizeof(struct eventblock));

	block->next = NULL;
	block->events = ev;

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

	// 在epoll_event
	event_set(&r->evblk->events[sockfd], sockfd, acceptor, r);

	// 将reactor指向的evblk指向的events的第sockfd个 
	event_add(r->epfd, EPOLLIN, &r->evblk->events[sockfd]);

	return 0;
}


int reactor_run(struct reactor *r) {
	if (r == NULL) return -1;
	if (r->epfd < 0) return -1;
	if (r->evblk == NULL) return -1;
	
	struct epoll_event events[MAX_EPOLL_EVENTS + 1];	// 注意要 + 1

	while (1) {
		// 每次最多从上百万的响应事件中取出 MAX_EPOLL_EVENTS + 1个
		int nready = epoll_wait(r->epfd, events, MAX_EPOLL_EVENTS, 1000);
		if (nready < 0) {
			printf("epoll_wait error, exit\n");
			continue;
		}

		int i;
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

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

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
	struct event *ev = (struct event *)malloc(MAX_EPOLL_EVENTS * sizeof(struct event));
	if (ev == NULL) {
		printf("reactor_alloc event failed\n");
		return -2;
	}
	memset(ev, 0, MAX_EPOLL_EVENTS * sizeof(struct event));

	// 初始化第一个 eventblock 块
	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (ev == NULL) {
		printf("reactor_alloc eventblock failed\n");
		return -2;
	}
	memset(block, 0, sizeof(struct eventblock));
	
	block->next = NULL;
	block->events = ev;

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