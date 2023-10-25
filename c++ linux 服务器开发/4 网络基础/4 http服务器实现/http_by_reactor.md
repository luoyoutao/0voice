### http 与 reactor 的关系

#### 对于accept_cb
可以实现：1、IP限制；2、负载均衡的功能。

#### 对于recv_cb
接收客户端http请求

#### 对于send_cb
发送http响应

### 说明

本节的webserver是在reactor框架的基础上搭建，因此大部分的函数没有变动，下面介绍有变动或新增的函数。

### read_line函数

```c
/*
* 功能：一行一行的读取数据
* 参数allbuf：所有buf
* 参数idx：从这个位置开读一行
* 参数linbuf：读取到一行信息放到这个起始地址
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
```

### http_request函数

```c
/*
* 功能：处理http请求
*/
int http_request(struct event *ev) {
	char linebuf[1024];
	int idx = readline(ev->buffer, 0, linebuf); // 读取一行信息

	if (strstr(linebuf, "GET") || strstr(linebuf, "get")) {	// 如果是GET请求
		ev->method = HTTP_METHOD_GET;
		
		// 找出get请求的url资源 
		int i  =0;
		while(linebuf[sizeof("GET ") + i]  != ' ') i ++;
		linebuf[sizeof("GET ") + i] = '\0';
		sprintf(ev->resource, "./%s/%s", HTTP_WEBSERVER_HTML_ROOT, linebuf + sizeof("GET "));

		// 查看资源文件是否存在
		int filefd = open(ev->resource, O_RDONLY);
		if (filefd == -1) {	// 资源文件不存在

		} else { // 资源文件存在
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
```

### recv_cb函数

```c
int recv_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = recv(fd, ev->buffer, BUFFER_LENGTH, 0);

	if (len > 0) {
		ev->length = len;
		ev->buffer[len] = '\0';
		printf("C[%d]:%s\n", fd, ev->buffer);

		http_request(ev); // 处理http请求

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
```

### http_response函数

```c
// 设置发送的内容
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
```

### send_cb函数

```c
int send_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	http_response(ev); // 设置发送的内容

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
```

