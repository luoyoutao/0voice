### 编写基本服务程序流程

#### 1、创建套接字

```c
#include <sys/types.h>
#include <sys/socket.h>
int socket(int domain, int type, int protocol);

/*
* 参数domain通讯协议族：
* PF_INET       IPv4互联网协议族
* PF_INET6      IPv6互联网协议族
* PF_LOCAL      本地通信的协议族
* PF_PACKET     内核底层的协议族
* PF_IPX        IPX Novell协议族
* IPv6尚未普及，其它的不常用
*/

/*
* 参数type数据传输的类型：
* SOCK_STREAM    面向连接的socket    数据不会丢失、顺序不会错乱、双向通道
* SOCK_DGRAM     无连接的socket     数据可能会丢失、顺序可能会错乱、传输效率更高
*/

/*
* 参数protocol最终使用的协议：
* 在IPv4网络协议家族中：
* 数据传输方式为SOCK_STREAM的协议只有IPPROTO_TCP；
* 数据传输方式为SOCK_DGRAM的协议只有IPPROTO_UDP。
* 本参数也可以填0，编译器自动识别。
*/

/*
* socket返回值：
* 成功返回一个有效的socket，失败返回-1，errno被设置
*/
```

#### 2、端口复用

```c
// 这个步骤非必须
int opt = 1;
unsigned int len = sizeof(opt);
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, len);
```

#### 3、设置IP和端口

```c
// 申请变量，用于存放协议、端口和IP地址
#include <netdb.h>
struct sockaddr_in servaddr;

// 初始化
memset(&servaddr,0,sizeof(servaddr));

// 设置协议族
servaddr.sin_family = AF_INET;

// 设置IP，本机的所有IP都可用
servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

// 指定用于监听的端口
servaddr.sin_port = htons(m_port);
```

#### 4、套接字与IP、端口绑定

```c
bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
// 失败返回-1
// 注意第二个参数要强制转换类型
```

#### 5、开始监听客户端

```c
int listen(int s, int backlog);

// 参数s：监听的描述符listen_fd
// 参数backlog：已经完成连接正等待应用程序接收的套接字队列长度
// 函数返回-1，表示失败
```

#### 6、接受连接上的客户端

```c
struct sockaddr_in client_addr;
socklen_t len = sizeof(client_addr);
int connet_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);

// 这里需要注意，同样涉及类型强制转换
// 最后一个参数必须传指针
```

#### 7、收发数据完成业务逻辑

```c
// 此处开启一个新的线程，来处理客户端的请求
#include <pthread.h>
pthread_t pid;
pthread_create(&pid, NULL, deal_request, (void*)(long)client_fd);

/*
* 处理客户端请求的逻辑：
* 读取数据成功，先输出，再直接发送过去
* 读取数据失败，关闭套接字
*/
void *deal_request(void* arg){
    // 处理流程
    return (void*)0;
}
```

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### 发送数据的函数

#### send

```c
ssize_t send(int sock_fd, const void *buf, size_t len, int flags);
/*
* 参数：
* sock_fd：发送给对方的网络套接字
* buf：待发送的数据的起始地址
* len：要发送的数据大小，发送最多不超过len大小的字节
* flags：用于控制发送行为，默认传0
*/

// 返回值是ssize_t，表示实际发送成功字节数。返回-1表示出错
```

#### write

```c
ssize_t write(int sock_fd, const void *buf, size_t count);
/*
* 参数：
* sock_fd：发送给对方的网络套接字
* buf：待发送的数据的起始地址
* count：最大写入字节数
*/
```

#### 补充

套接字为阻塞模式的时候，如果发送缓冲区无法容纳发送的数据，程序会阻塞在send和write方法。send、write函数向套接字发送数据时，函数调用完不表示数据已经发送出去。网络协议栈有一个发送缓冲区，先将数据拷贝到发送缓冲区，然后网络协议栈将发送缓冲数据通过网卡驱动转为电信号给发送出去。阻塞模式下，发送缓冲区空间不够，程序阻塞在send、write函数，直到发送缓冲区数据发送出去腾出空间，将剩下数据再拷贝到腾出的空间，直接到数据全部拷贝进发送缓冲区，函数返回。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### 接收数据的函数

#### recv

```c
ssize_t recv(int sock_fd, void *buf, size_t len, int flags);

/*
* 参数：
* sock_fd：从哪个套接字接收数据
* buf：接收到的数据保存到以buf为起始的地址
* len：本次最多接收多少字节的数据
* flags：控制套接字接收行为，默认传0
*/

/*
* 返回值：
* -1：出错，可以在errno取到对应的错误信息
* 大于0：实际读取到的字节数
* 0(EOF)：对端没有更多数据发送了，可能对端已经把连接关闭
*/
```

#### read

```c
ssize_t read (int sock_fd, void *buf, size_t count);

/*
* 参数：
* sock_fd：从哪个套接字接收数据
* buf：接收到的数据保存到以buf为起始的地址
* count：本次最多接收多少字节的数据
*/

/*
* 返回值：
* -1：出错，可以在errno取到对应的错误信息
* 大于0：实际读取到的字节数
* 0(EOF)：对端没有更多数据发送了，可能对端已经把连接关闭
*/
```

#### 补充

套接字阻塞模式下，如果调用read和recv函数的时候，套接字没有数据可读，程序会阻塞在read或者recv函数这里，直到有数据可读。调用read、recv函数时，从接收缓冲区读数据。所以不能保证每次调用read、recv函数时一定能读出所有数据。因为，数据可能还在对端发送缓冲区，也可能还在各个中间设备(路由器、交换机、电信主干网)。接收缓冲区中有个读指针和写指针，当调用read和recv函数时，读指针会往后移，下次读取就从新的指针处开始读，读指针移动的长度就是read和recv函数返回的实际读取到的字节长度。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### 分包粘包问题

#### 分包

对方发送helloworld，我方收到hello和world。

#### 粘包

对方发送hello和world，我方收到helloworld。

#### 解决办法

TCP协议中，数据以字节流方式传输，被发送的数据可能不是一次性发完，可能是被拆成很多个小段，一段段发出去。有个重要前提，就是$\textcolor{red}{TCP协议可以保证数据的顺序性}$。因此，可以采用$\textcolor{red}{报文长度 + 报文内容}$的方法。也可以使用特殊的分隔符，如$\textcolor{red}{http协议采用\r\n}$。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### 阻塞与非阻塞

#### 阻塞

阻塞I/O调用recv、read时，程序切换到内核态。若I/O中（套接字）没有数据可读，就阻塞。直到有数据可读，内核将数据复制到用户态，复制完再返回。最后recv、read函数再返回读取到的字节数。缺点是这种方式比较占CPU。

#### 非阻塞

非阻塞模式下，没有读取到数据立即返回-1，为了区别阻塞模式下返回-1，可以查看错误码errno设置成了EAGAIN。缺点就是需要间隔一段时间就读一下数据涉及系统调用，还是比较消耗资源。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### 小注意

#### 设置非阻塞

```c
#include <unistd.h>
#include <fcntl.h>
fcntl(fd, F_SETFL, O_NONBLOCK);
```

#### 设置端口复用

```c
int opt = 1;
unsigned int len = sizeof(opt);
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, len);
```

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### I/O复用之select

#### 原理

select方法告诉内核程序自己关心哪些I/O描述符，当内核程序发现其中有I/O准备好（可写/可读/异常），内核程序将数据复制到用户态并从select返回。用户拿着准备好的IO再调用recv就一定能拿到数据。

#### 用法和参数

```c
int select(
  int maxfdp1,
  fd_set *readset,
  fd_set *writeset,
  fd_set *exceptset,
  const struct timeval *timeout
);

/*
* 参数：
* maxfdp1：当前待监听描述符基数。若监听最大描述符是3，那么参数maxfdp1就应该是4（因为描述符从0开始）
* fd_set：通常有读、写、异常三种情况，readset、writeset、exceptset分别对应I/O的读、写和异常。
  表示当前关心readset里描述符是否可读，writeset里的描述符是否可写，exceptset中的描述符是否出现异常。
* 参数timeout：
  struct timeval {
      long tv_sec;  // 秒
      long tv_usec; // 微秒
  }
	- 传NULL，如果没有I/O可以处理，一直等待；
	- 设置成对应的秒或微秒，等待相应时间后若没有I/O可以处理就返回；
	- 将tv_sec和tv_usec都设置成0，表示不用等待立即返回。
*/

/*
* select返回值：
* -1表示出错；
* 0表示超时；
* 大于0表示可操作的I/O数量。
*/
```

#### 使用流程(服务端)

```c
// 1、创建监听的fd集合并初始化
#include <sys/select.h>
fd_set readfds;    // 大小16字节，1024位
FD_ZERO(&readfds); // 每一位置0

// 2、把listen的socket加入集合
#include <sys/select.h>
FD_SET(listensock, &readfds);  // 起初还未有客户端加进来

// 3、while循环中调用select
fd_set tmpfds = readfds;	// 复制一份fd集合，因为系统在判断时会更改送进去集合参数
int infds = select(maxfd + 1, &tmpfds, NULL, NULL, 0);

// 4、当select返回值大于0
// 用FD_ISSET判断每个socket是否有事件
FD_ISSET(eventfd, &tmpfds)

/*
对于新连接进来的客户端加入到事件集合
FD_SET(clientsock, &readfds);
*/

/*
对于断开的客户端，将其清除
FD_CLR(eventfd, &readfds);
并将套接字关闭
close(eventfd); 
*/
```

#### 触发方式

采用水平触发方式，如果报告fd事件没有被处理或数据没有被全部读取，下次select时会再次报告该fd。

#### 缺点

1、select支持文件描述符数量太小，默认1024;

2、每次调整select都需要把fdset从用户态拷贝到内核;

3、同时在线的大量客户端有事件发生的可能小，但还是需要遍历fdset，因此随着监视的描述符数量增长，效率也会线性下降。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### I/O复用之poll

#### 原理

与select本质上没有差别，管理多个描述符也进行轮询，根据描述符状态进行处理，但是poll没有最大文件描述符数量的限制。

#### 用法和参数

```c
int poll(
    struct pollfd *fdarr,
    unsigned long nfds,
    int timout
);

/*
* 参数：
* fdarr：要监听的I/O描述符事件集合，其结构如下：
  struct pollfd {
      int fd; // 描述符
      short events;  // 监听描述符发生的事件
      short revents; // 已经发生的事件
  };
* nfds：要监听的套接字数量
* timeout：超时时间，一般有三种传值的方式
	- -1表示在有可用描述符之前一直等待；
	- 0表示不管有没有可用描述符都立即返回；
	- 大于0表示超过对应毫秒即使没有事件发生也会立即返回
*/

/*
* poll返回值：
* -1表示出错；
* 0表示超时；
* 大于0表示可操作的I/O数量。
*/
```

#### 使用流程(服务端)

```c
// 1、声明struct pollfd类型数组fds
int maxfd = 2047;
struct pollfd fds[maxfd + 1]; 

// 2、初始化fds所有位置为-1，表示忽略该元素，poll在查找事件时就不会遍历这个元素。
fds[i].fd = -1;

// 3、将服务端套接字放到fds第一个位置，并设置监听POLLRDNORM事件
fds[0].fd = listensock;
fds[0].events = POLLIN;  // 读事件

// 4、循环里调用poll函数
int infds = poll(fds, maxfd + 1, 10); // 超时时间为10毫秒

/*
* 5、当poll返回值大于0
* 先判断fds[i].fd是否为-1，若不为，再通过fds[eventfd].revents&POLLIN
* 判断某个描述符是否有读事件
*/

/*
* 如果是listensock
* 接受新的客户端连接，将新套接字
* 找个-1的空位置存放起来，并监听POLLRDNORM事件
*/

/*
* 如果是原先存在的客户端事件，则做相应的业务处理：
* 读取数据成功，继续相应的业务处理操作
* 读取失败，将该位置设置为-1，fds[i].fd = -1;
* 且关闭套接字，close(fds[i].fd)
*/

```

#### 与select异同

相同点：

poll和select都会遍历所有描述符，在连接数非常大时有性能问题，而epoll就很好的解决该个问题。

不同点：

1、select采用fd_set和bitmap，而poll采用数组；

2、在声明pollfd结构数据的时候，可以自行指定大小；

3、select会修改fd_set，因此需要复制一份。poll不会修改pollfd，它通过pollfd的events指定要监听的事件，再通过revents保存已发生事件用于在poll返回时判断都有哪些事件发生。poll用两个short整型来保存监听事件，和已经发生的事件。意味着，调用poll之前不需要将监听的事件复制一份。I/O设置监听事件使用events，判断是否有事件发生使用revents。

#### 缺点

与select类似，poll文件描述符数组被整体复制于用户态和内核态的地址空间之间，不论这些文件描述符是否有事件，开销随着文件描述符数量增加而线性增大。poll返回后，也需要历遍整个描述符数组才能得到有事件的描述符。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### I/O复用之epoll

#### 使用流程(服务器)

```c
// 1、创建epoll实例
int epoll_create(int size);
int epoll_create1(int flags);
// 参数：一般传0即可
// 返回值：大于0表示epoll实例，-1表示出错

// 2、注册要监听的fd和事件
int epoll_ctl(
  int epfd,
  int op,
  int fd,
  struct epoll_event *event
);
/*
* 参数：
* epfd：使用poll_create创建出的epoll实例
* op：表示增、删、改分别对应:
  EPOLL_CTL_ADD：向epoll实例注册文件描述符对应的事件；
  EPOLL_CTL_DEL：删除epoll实例中文件描述符对应的事件；
  EPOLL_CTL_MOD：修改epoll实例中文件描述符对应的事件。
* fd：要注册事件的描述符，这里指网络套接字。
* event：是一个结构体，如下：
  struct epoll_event {
      uint32_t events;   // epoll事件
      epoll_data_t data;
  };
  - typedef union epoll_data {
      void *ptr;
      int fd;
      uint32_t u32;
      uint64_t u64;
    } epoll_data_t;
  对于events一般设置如下：
  这里的事件与poll的基本一样
  下面是在使用epoll的时候，常用的事件类型：
  EPOLLIN：表示描述符可读
  EPOLLOUT：表示描述符可写
  EPOLLRDHUP：表示描述符一端已经关闭或者半关闭
  EPOLLHUP：表示对应描述符被挂起
  EPOLLET：边缘触发模式edge-triggered，不设置默认使用
*/

/*
* epoll_ctl返回值：
* 0表示成功
* -1表示出错
*/

// 3、等待事件发生
int epoll_wait(
  int epfd,
  struct epoll_event *events,
  int maxevents,
  int timeout
);
/*
* 参数：
* epfd：使用poll_create创建出的epoll实例
* events：要处理的I/O事件，是个数组，大小是epoll_wait的返回值，每一个元素是一个待处理的I/O事件。
* maxevents：epoll_wait可以返回的最大事件
* timeout：超时时间，和select基本是一致的。
  - 如果设置-1表示不超时；
  - 设置0表示立即返回；
*/

/*
* epoll_wait返回值：
  大于0表示事件个数；
  0表示超时；
  -1表示出错。
*/
```

#### 与poll区别

1、epoll需要使用poll_create创建一个实例，后续所的操作都基于这个实例；

2、epoll不再是将fd设置成-1来表示忽略当前描述，而是关心哪个就设置哪个，使用epoll_ctl函数；

```c
// 例如：
struct epoll_event event;
event.data.fd = sock_fd;
event.events = EPOLLIN | EPOLLET;
epoll_ctl(efd, EPOLL_CTL_ADD, sock_fd, &event)
```

3、events返回所有实际产生事件集合，大小就是epoll_wait返回值。所以，poll_wait返回，就可以确定从0到read_num所有位置都是有事件发生。而poll每次都从0遍历到最大描述字。这中间有很多没有事件发生的描述字。epoll这种实现绕不开它背后的数据结构-$\textcolor{red}{红黑树}$。

#### 触发方式

$\textcolor{green}{边沿触发：}$

只在第一次有数据可读的情况下通知一次。后面的处理就完全靠自己了，很显然这种触发方式能够明显减少触发次数，从而减轻内核的压力，这在一些大数据量的传输场景下非常有用。

$\textcolor{green}{水平|条件触发（epoll默认）：}$

每次有数据可读的时候都会触发事件，在某些情况下会造成内核频发触发事件。

$\textcolor{Magenta}{++++++++++++++++++++++++++++++++++++++}$

### 几种I/O模式

#### 一请求一线程模式

##### 代码 base_server.c

```c
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
```

##### 特点

在一个while(1)循环，一直accept 客户端的连接。来一个客户端，就为其分配一个线程，去处理请求。
每个线程里面，也是while(1)循环不断处理每个客户端的请求。

##### 优点

逻辑简单。

##### 缺点

不适合大量的客户端请求，无法突破C10K。

#### select模式

##### 代码 select_server.c

```c
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
				printf("client [%d] was connected!\n", client_fd);
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
```

##### 特点

maxfd有最大限制，1024。通过多设置几个select，相比方法1，能突破C10K，但是难以突破C1000K，因为每次调用fd_set需要copy进内核，然后返回再copy出来，涉及系统调用，当大量copy时，还是有限制的。

#### poll模式

##### 代码 poll_server.c

```c
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
```

##### 特点

与select差不多，都是采用轮询，比较消耗资源，只不过maxfd没有了1024的限制。

#### epoll模式

##### 代码 epoll.server.c

```c
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
```

##### 特点

不再是将fd设置成-1来表示忽略当前描述，而是关心哪个就设置哪个。events返回所有实际产生事件集合，大小就是epoll_wait返回值。

#### 三种I/O模式对比

num of fd     |     select cpu time     |     poll cpu time     |     epoll cpu time

​      10            |               0.73               |            0.61             |               0.41

​     100           |                 3                  |             2.9               |              0.42 

​    1000          |                35                 |              35               |              0.53

   10000         |               930                |             990              |              0.66



