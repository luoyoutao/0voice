### epoll数据结构

对于被管理的fd，有两个集合。

1、所有被管理的fd集合

2、就绪的fd的集合，即可读可写的fd的集合

其中，所有管理的集合涉及查找操作，对比哈希、红黑树、b树b+树三种的优缺点，综合考虑使用红黑树。因为哈希刚开始的时候浪费空间，而且后续增加数量超过一定数量，还得重新散列一下。b树b+树能降低树的层级，但是查找的次数不会低于红黑树。

就绪的fd的集合使用队列，先进先出，现有事件的fd先处理。

### 与select、poll的差异

select、poll每次检查fd是否有事件时，需要把所有被管理的总集合cpoy进内核协议栈，再copy出来到用户空间。再去轮询检测每一个fd是否有事件。而epoll通过epoll_ctl将fd加入集合。如下：

![epoll工作环境](C:\Users\Administrator\Desktop\0voice笔记\6 自研协程框架\epoll实现\epoll工作环境.png)



### 协议栈如何与epoll通信时机

![通知epoll](C:\Users\Administrator\Desktop\0voice笔记\6 自研协程框架\epoll实现\通知epoll.png)

### 从协议栈回调通知epoll

1、传的参数

fd、事件（epollin、epollout）

2、回调操作

a、通过fd查找对应的节点

b、把节点加入到就绪队列

### epoll三个API里面的操作

1、epoll_create

创建一个红黑树的根节点

2、epoll_ctl

三个操作：添加ADD、删除DEL、修改MOD

3、epoll_wait

将就绪队列里面的节点copy到用户空间

### epoll如何加锁

epoll_ctl对红黑树加锁（互斥锁）

epoll_wait对就绪队列加锁（自旋锁）

### et与lt

et 边沿触发

lt 水平触发

这里的触发，指的是协议栈 回调

et 接收到数据，调用一次回调

lt 检测到recvbuffer里面有数据，就调用回调