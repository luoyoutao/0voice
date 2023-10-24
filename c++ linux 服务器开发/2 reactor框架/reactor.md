### 背景

​	网络IO涉及两个系统对象，一个是用户空间调用IO的进程或者线程，另一个是内核空间的内核系统，它会经历两个阶段：1、等待数据准备就绪；2、将数据 从内核拷贝到进程或者线程中。因为在以上两个阶段上各有不同的情况，所以出现了多种网络IO模型。

​	当使用默认阻塞套接字时（例如1个线程捆绑处理1个连接），往往是把这两个阶段合而为一，这样操作套接字的代码所在的线程就得睡眠来等待消息准备好，这导致了高并发下线程会频繁的睡眠、唤醒，从而影响了CPU的使用效率。

​	高并发编程方法把两个阶段分开处理。即，$\textcolor{red}{等待消息准备好的代码段，与处理消息的代码段是分离的}$。这就要求套接字必须是$\textcolor{red}{非阻塞的}$。那么此时，等待消息准备好这个阶段就要让线程主动查询，或者让 1 个线程为所有连接而等待！这就是 IO 多路复用了。$\textcolor{Green}{多路复用就是处理等待消息准备好这件事，它可以同时处理多个连接！}$它也可能等待，所以它也会导致线程睡眠，然而这不要紧，因为它一对多、它可以监控所有连接。这样，当我们的线程被唤醒执行时，就一定是有一些连接准备好被我们的代码执行了。

### reactor原理

Reactor模式是处理并发I/O比较常见的一种模式，用于同步I/O，中心思想是将所有要处理的I/O事件注册到一个中心I/O多路复用器上，同时主线程/进程阻塞在多路复用器上；一旦有I/O 事件到来或是准备就绪(文件描述符或socket 可读、写)，多路复用器返回并将事先注册的相应I/O 事件分发到对应的处理器中。
Reactor模型有三个重要的组件：

$\textcolor{red}{多路复用器}$：由操作系统提供，在linux 上一般是select, poll, epoll 等系统调用。

$\textcolor{red}{事件分发器}$：将多路复用器中返回的就绪事件分到对应的处理函数中。

$\textcolor{red}{事件处理器}$：负责处理特定事件的处理函数。

### reactor流程

具体流程如下：

$\textcolor{Magenta}{1、注册读就绪事件和相应的事件处理器；}$

$\textcolor{Magenta}{2、事件分发器等待事件；}$

$\textcolor{Magenta}{3、事件到来，激活分发器，分发器调用事件对应的处理器；}$

$\textcolor{Magenta}{4、事件处理器完成实际的读操作，处理读到的数据，注册新的事件，然后返还控制权。}$

### reactor优点

* 编程相对简单，可以最大程度的避免复杂的多线程及同步问题，并且避免了多线程/进程的切换开销; 

* 可扩展性，可以方便的通过增加Reactor实例个数来充分利用CPU资源;
* 可复用性，reactor 框架本身与具体事件处理逻辑无关，具有很高的复用性;

### reactor代码结构

#### 宏定义(4)

```c
#define BUFFER_LENGTH		  1024  // 每个fd对应的buf长度
#define MAX_EPOLL_EVENTS	1024 	// 每一个事件块最多存1024个事件
#define SERVER_PORT			  8888  // 起始端口号
#define PORT_COUNT        100  	// 服务端开的端口数量
```

#### 相关结构体(3)

```c
// 事件结构体
struct event {
	int fd;
	int events;
	void *arg;
	int (*callback)(int fd, int events, void *arg); // 回调函数指针，处理不同事件
	
	int status;
	char buffer[BUFFER_LENGTH];
	int length;
};

// 指向事件块的指针
struct eventblock {
	struct event *events;
	struct eventblock *next;
};

// reactor结构体
struct reactor {
	int epfd;	// epfd 实例
	int blkcnt;	// 块u数量
	struct eventblock *evblk;	// 第一个块指针
};
```

#### 服务器初始化(1)

```c
// 在某个端口上 绑定IP和fd
int init_sock(short port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(fd, F_SETFL, O_NONBLOCK);		// 设置为非阻塞

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);
	// 绑定 IP 端口
	bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
	// 监听
	if (listen(fd, 20) < 0) {
		printf("listen failed : %s\n", strerror(errno));
	}

	return fd;
}
```

#### reactor初始化(1)

```c
// 需要在函数调用前分配内存
struct reactor *r = (struct reactor*)malloc(sizeof(struct reactor));

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
		printf("event_alloc event failed\n");
		return -2;
	}
	memset(ev, 0, MAX_EPOLL_EVENTS * sizeof(struct event));

	// 初始化第一个 eventblock 块
	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (ev == NULL) {
		printf("event_alloc eventblock failed\n");
		return -2;
	}
	memset(block, 0, sizeof(struct eventblock));
	
  // 连接起来，并设置好末尾
	block->next = NULL;
	block->events = ev;
	
	r->evblk = block;
	r->blkcnt = 1;

	return 0;
}
```

#### reactor销毁(1)

```c
int reactor_destory(struct reactor *r) {
	close(r->epfd);
	
  // 用两个变量来交替
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
```

#### 添加event块(1)

```c
int event_alloc(struct reactor *r) {
	if (r == NULL) return -1;
	if (r->evblk == NULL) return -1;
	struct eventblock *blk = r->evblk;
	while (blk->next != NULL) {	// 找到最后一块event
		blk = blk->next;
	}
  
  // 新开辟一块内存，存放event结构体
	struct event *ev = (struct event *)malloc(MAX_EPOLL_EVENTS * sizeof(struct event));
	if (ev == NULL) {
		printf("event_alloc event failed\n");
		return -2;
	}
	memset(ev, 0, MAX_EPOLL_EVENTS * sizeof(struct event));
  
  // 新开辟一块内存，存放eventblock结构体
	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (ev == NULL) {
		printf("event_alloc eventblock failed\n");
		return -2;
	}
	memset(block, 0, sizeof(struct eventblock));

	block->next = NULL;
	block->events = ev;

	blk->next = block;
	r->blkcnt ++;	// 记录的内存块数量加1

	return 0;
}
```

#### event处理函数(4)

```c
// 通过fd找到对应的event块位置
struct event * find_event_byfd(struct reactor* r, int fd) {
	int blkid = fd / MAX_EPOLL_EVENTS;	//	计算出在第几个event块
	while (blkid >= r->blkcnt) {	// fd应在块数id大于等于目前的块数
		event_alloc(r);
	}

	// 查找到该块的位置
	int i = 0;
	struct eventblock *blk = r->evblk;
	while (i < blkid && blk != NULL) {
		blk = blk->next;
		i ++;
	}
	
	return &blk->events[blkid % MAX_EPOLL_EVENTS];	// 返回该块中具体某个位置的event
}

// 给ev结构体设置对应的值
void event_set(struct event *ev, int fd, NCALLBACK callback, void *arg) {
	ev->fd = fd;
	ev->callback = callback;
	ev->events = 0;
	ev->arg = arg;
}

/* 
* 功能：添加event事件
* 参数epfd：epoll实例
* 参数events：具体的事件（宏）
* 参数*ev：struct event结构体
*/
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

// 删除事件
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
```

#### $\textcolor{red}{注意}$

在上面的处理函数中，$\textcolor{red}{ep_ev.data.ptr = ev}$语句，通过将epoll_event结构体中的data的ptr指针指向我们自己定义的event结构体，进而能将fd和回调函数绑定起来，当通过epoll_wait函数获取事件fd时，能直接调用该fd的回调函数。

#### 回调函数(3)

```c
// 函数指针，用作回调函数
// 参数分别是fd、event具体事件、reactor指针
typedef int (*NCALLBACK)(int ,int, void*);

/*
* 功能：接受已经连接的客户端的请求
* 参数fd：客户端socket描述符
* 参数events：相应的事件（宏）
* 参数*arg：reactor指针
*/
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

	// 然后设置clientfd的监听事件以及回调函数
	event_set(ev, clientfd, recv_cb, r);
	event_add(r->epfd, EPOLLIN, ev);

	printf("new connect [%s:%d], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), clientfd);

	return 0;
}

/*
* 功能：从缓冲区读取客户端发送来的数据
* 参数fd：客户端socket描述符
* 参数events：相应的事件（宏）
* 参数*arg：reactor指针
*/
int recv_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = recv(fd, ev->buffer, BUFFER_LENGTH, 0);
	if (len > 0) {
		ev->length = len;
		ev->buffer[len] = '\0';

		printf("C[%d]:%s\n", fd, ev->buffer);
    
    // 先删除当前事件，再监听写事件和设置回调函数，等待下一次的读事件发生
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

/*
* 功能：向客户端发送数据
* 参数fd：客户端socket描述符
* 参数events：相应的事件（宏）
* 参数*arg：reactor指针
*/
int send_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = send(fd, ev->buffer, ev->length, 0);
	if (len > 0) {
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);
		
    // 先删除当前事件，再监听读事件，等待下一次的读事件发生
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
```

#### 监听服务端socket读事件(1)

```c
/*
* 功能：监听listen_fd的读事件
* 参数r：reactor
* 参数sockfd：listen socket fd
* 参数acceptor：回调函数，传参accept_cb
*/
int reactor_addlistener(struct reactor *r, int sockfd, NCALLBACK acceptor) {
	if (r == NULL) return -1;
	if (r->evblk == NULL) return -1;

	// 在epoll_event
	event_set(&r->evblk->events[sockfd], sockfd, acceptor, r);
	// 将reactor指向的evblk指向的events的第sockfd个 
	event_add(r->epfd, EPOLLIN, &r->evblk->events[sockfd]);

	return 0;
}
```

#### reactor跑起来(1)

```c
int reactor_run(struct reactor *r) {
	if (r == NULL) return -1;
	if (r->epfd < 0) return -1;
	if (r->evblk == NULL) return -1;
	
  /*
  * 实际可能会有上百万个fd事件要监听
  * 但是此处，每次只取MAX_EPOLL_EVENTS + 1个
  * 因此需要多取几次可能才能取完有事件的fd
  */
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
```

#### 主函数(1)

```c
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
		sock_fds[i] = init_sock(port + i);		// 端口从8888开始
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
```

### reactor服务器结构图

#### 事件结构图

![reactor模型事件存储结构](C:\Users\Administrator\Desktop\0voice笔记\4 网络编程\2 reactor\reactor模型事件存储结构.png)

#### reactor模型流程图

![reactor模型流程](C:\Users\Administrator\Desktop\0voice笔记\4 网络编程\2 reactor\reactor模型流程.png)



### 所有代码(reactor.c)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_LENGTH		1024
#define MAX_EPOLL_EVENTS	1024 	// 每一个事件块最多存1024个事件
#define SERVER_PORT			8888
#define PORT_COUNT 1  	// 服务端开的端口数量

typedef int (*NCALLBACK)(int ,int, void*);

struct event {
	int fd;
	int events;
	void *arg;
	int (*callback)(int fd, int events, void *arg);
	
	int status;
	char buffer[BUFFER_LENGTH];
	int length;
};

struct eventblock{
	struct event *events;
	struct eventblock *next;
};

struct reactor {
	int epfd;	// epfd 实例
	int blkcnt;	// 块u数量
	struct eventblock *evblk;	// 第一个块指针
};

int recv_cb(int fd, int events, void *arg);
int send_cb(int fd, int events, void *arg);

struct event * find_event_byfd(struct reactor* r, int fd) {
	int blkid = fd / MAX_EPOLL_EVENTS;	//	计算出在第几块
	while (blkid >= r->blkcnt) {
		event_alloc(r);
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
	event_del(r->epfd, ev);

	if (len > 0) {
		ev->length = len;
		ev->buffer[len] = '\0';

		printf("C[%d]:%s\n", fd, ev->buffer);

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
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

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

	// 根据fd找到event应该在events线性表中的具体位置
	struct event * ev = find_event_byfd(r, fd);

	// 然后设置clientfd的监听事件
	event_set(ev, clientfd, recv_cb, r);
	event_add(r->epfd, EPOLLIN, ev);

	printf("new connect [%s:%d], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), clientfd);

	return 0;
}

int event_alloc(struct reactor *r) {
	if (r == NULL) return -1;
	if (r->evblk == NULL) return -1;
	struct eventblock *blk = r->evblk;
	while (blk->next != NULL) {
		blk = blk->next;
	}
	struct event *ev = (struct event *)malloc(MAX_EPOLL_EVENTS * sizeof(struct event));
	if (ev == NULL) {
		printf("event_alloc event failed\n");
		return -2;
	}
	memset(ev, 0, MAX_EPOLL_EVENTS * sizeof(struct event));

	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (ev == NULL) {
		printf("event_alloc eventblock failed\n");
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
		printf("event_alloc event failed\n");
		return -2;
	}
	memset(ev, 0, MAX_EPOLL_EVENTS * sizeof(struct event));

	// 初始化第一个 eventblock 块
	struct eventblock *block = (struct eventblock *)malloc(sizeof(struct eventblock));
	if (ev == NULL) {
		printf("event_alloc eventblock failed\n");
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
		sock_fds[i] = init_sock(port + i);		// 端口从8888开始
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
```

