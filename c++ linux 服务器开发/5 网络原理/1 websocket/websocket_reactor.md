### 背景

在发送长度较短的信息时，http协议的利用率就不高。因此websocket出现了，可以自定义协议。

### websocket组成

websocket由两部分组成：1、tcp包本身的信息（如长度）；2、业务协议

### websocket应用

1、即时通讯；2、浏览器与服务器之间实时通信

### websocket协议格式

链接建立后，第一个数据包符合http协议头。

然后一行一行读取数据，当读取到某行有Sec-WebSocket-Key: ，就将其后面的值和固定字符"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"拼接起来，如dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11，得到accept-key，将accept-key和其他信息发回给客户端，即完成身份验证。

后续的操作，就是双方发送数据，对于客户端发送来的数据，不同长度有不同的格式。如下：

![websocket数据包格式](C:\Users\Administrator\Desktop\0voice笔记\5 网络原理\0 websocket\websocket数据包格式.png)

服务器收到数据后，还需要对payload解码，才能得到正确的数据。

解码方法为$\textcolor{red}{payload[i] = payload[i]\^mask\_key[i\%4]}$

### websocket处理流程

![websocket通讯基本原理](C:\Users\Administrator\Desktop\0voice笔记\5 网络原理\0 websocket\websocket通讯基本原理.png)

### websocket相关宏和结构体

```c

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
```

### 相关重要函数

#### accept_cb

```c
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
	
  // 初始化状态为WS_HANDSHARK
	ev->status_machine = WS_HANDSHARK;
	// 然后设置clientfd的监听事件
	event_set(ev, clientfd, recv_cb, r);
	event_add(r->epfd, EPOLLIN, ev);

	printf("new connect [%s:%d], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), clientfd);

	return 0;
}
```

#### websocket_request

```c
int websocket_request(struct event *ev) {
	if (ev->status_machine == WS_HANDSHARK) {
		ev->status_machine = WS_TRANMISSION;
		handshark(ev);
	} else if(ev->status_machine == WS_TRANMISSION) {
		transmission(ev);
	} else {

	}
  return 0;
}
```

#### recv_cb

```c
int recv_cb(int fd, int events, void *arg) {
	struct reactor *r = (struct reactor*)arg;
	struct event *ev = find_event_byfd(r, fd);

	int len = recv(fd, ev->buffer, BUFFER_LENGTH, 0);

	if (len > 0) {
		ev->length = len;
		ev->buffer[len] = '\0';
		
    // 处理客户端的请求
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
```

#### base64_encode

```c
// 64编码
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
```

#### handshark

```c
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
```

#### umask

```c
// 解码
void umask(char *payload, int length, char *mask_key) {
	int i = 0;
	for (; i < length; i ++) {
		payload[i] ^= mask_key[i % 4];
	}
}
```

#### transmission

```c
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
```

