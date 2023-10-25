### socket

插 座

内核中是 tcb (tcp control block，即tcp控制块)

int fd = socket();

fd = 3开始

因为0、1、2是stdin、stdout、stderr，即标准输入、标准输出、标准错误

### bind

主要是绑定端口。与socket都是作用本地。

### 三次握手

![三次握手](C:\Users\Administrator\Desktop\0voice笔记\5 网络原理\3 Posix API与网络协议栈\三次握手.png)

### 做到100万连接

服务器的端口只有65535，为啥能做到100万连接？因为fd和五元组有关，且可以端口复用。

### 粘包和分包

由于发送数据是由内核层自主控制，因此，可能存在粘包和分包的问题。

解决这个问题可以有两个方案：

1、用分隔符，如http协议使用\r\n

2、包长 + 包内容

能用这两种方案解决的前提是：

tcp能保证包的顺序性。其根本原因是tcp的延迟ACK确认机制。

### 四次挥手

![四次挥手](C:\Users\Administrator\Desktop\0voice笔记\5 网络原理\3 Posix API与网络协议栈\四次挥手.png)

### 为什么要有time_wait

避免死锁，因为被动方要接受ack，超时（120秒）会重传fin，如果没有time_wait，主动方就回收了fd和tcb，无法收到被动方发的重传包，主动方也就无法再次发送ack包，这样被动方就会一直等待。
