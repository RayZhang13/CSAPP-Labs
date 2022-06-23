#include <stdio.h>
#include <stdlib.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define METHOD_MAX_LEN 7          //请求方法最大长度
#define HOST_MAX_LEN 2048         //请求主机名最大长度
#define PORT_MAX_LEN 6            //端口名称最大长度
#define PATH_MAX_LINE 2048        //请求路径最大长度
#define VERSION_MAX_LEN 10        // HTTP版本最大长度
#define HEADER_NAME_MAX_LEN 20    //请求头名称最大长度
#define HEADER_VALUE_MAX_LEN 200  //请求头内容最大长度
#define HEADER_MAX_NUM 20         //请求头最大数量

#define THREAD_NUM 4  //创建的工作线程数量
#define SBUFSIZE 16   //创建的buf数组大小

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

typedef struct {
    char method[METHOD_MAX_LEN];    //请求方法
    char host[HOST_MAX_LEN];        //请求主机名
    char port[PORT_MAX_LEN];        //请求端口号
    char path[PATH_MAX_LINE];       //请求路径
    char version[VERSION_MAX_LEN];  // HTTP版本
} Request_Line;

typedef struct {
    char name[HEADER_NAME_MAX_LEN];    //请求头名称
    char value[HEADER_VALUE_MAX_LEN];  //请求头内容
} Request_Header;

typedef struct {
    Request_Line request_line;                       //一个请求行
    Request_Header request_headers[HEADER_MAX_NUM];  //若干个请求头
    int request_header_num;                          //请求头数量
} Request;

typedef struct {
    int *buf;    /* Buffer array */
    int n;       /* Maximun number of slots */
    int front;   /* buf[(front + 1) % n] is the first item */
    int rear;    /* buf[rear % n] is the last item */
    sem_t mutex; /* Protects access to buf */
    sem_t slots; /* Counts available slots */
    sem_t items; /* Counts available items */
} sbuf_t;

typedef struct {
    int valid;                       //缓存有效标志位
    char request[MAXLINE];           // request作为key
    char response[MAX_OBJECT_SIZE];  //内容作为value
    int time_stamp;                  //记录修改时间戳
} Cache_Pair;

typedef struct {
    sem_t mutex;            //保护cache_set
    Cache_Pair *cache_set;  //存储多个缓存对Cache Pair
    int cache_pair_num;     //记录缓存对数量
} Cache;

void read_request(int fd, Request *request);
void read_request_line(rio_t *rp, char *buf, Request_Line *request_line);
void read_request_header(rio_t *rp, char *buf, Request_Header *request_header);
void parse_uri(char *uri, char *host, char *port, char *path);
void doit(int fd);
int send_request(Request *request);
void forward_response(int connfd, int targetfd, Request *request);
void sigpipe_handler(int sig);
void *thread(void *vargp);

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

void cache_init(Cache *cache, int n);
int cache_find(Cache *cache, Request *request, int fd);
void cache_insert(Cache *cache, Request *request, char *response);

sbuf_t sbuf;            /* Shared buffer of connected descriptors */
Cache cache;            /* Cached response content */
int cur_time_stamp = 0; /* Init time stamp */

int main(int argc, char **argv) {
    int listenfd, connfd;  //服务端的监听文件描述符和连接文件描述符
    char hostname[MAXLINE], port[MAXLINE];  //客户端的主机名和端口
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  //套接字地址
    pthread_t tid;

    /* Check command-line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    Signal(SIGPIPE, sigpipe_handler);  // SIGPIPE信号处理程序

    listenfd = Open_listenfd(argv[1]);  //服务端监听指定端口

    sbuf_init(&sbuf, SBUFSIZE);                            //初始化缓冲区
    cache_init(&cache, MAX_CACHE_SIZE / MAX_OBJECT_SIZE);  //初始化缓存

    for (int i = 0; i < THREAD_NUM; i++) {  //创建THREAD_NUM个工作线程
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(
            listenfd, (SA *)&clientaddr,
            &clientlen);  // accept创建连接文件描述符，同时获取到客户端地址等信息保存到clientaddr中
        Getnameinfo(
            (SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
            0);  //根据clientaddr还原出客户端主机名和端口信息并保存在hostname和port
        printf("Accepted connection from (%s, %s)\n", hostname,
               port);                //打印连接的客户端信息
        sbuf_insert(&sbuf, connfd);  //将连接加入缓冲区
    }
}

void *thread(void *vargp) {
    Pthread_detach(Pthread_self());  //将线程进行分离
    while (1) {
        int connfd = sbuf_remove(&sbuf);  //从缓冲区消费获取连接
        doit(connfd);                     //处理连接
        Close(connfd);                    //关闭连接
    }
}

void sigpipe_handler(int sig) {
    fprintf(stderr, "Error: Connection reset by peer!\n");
}

void cache_init(Cache *cache, int n) {
    Sem_init(&cache->mutex, 0, 1);                     //初始化mutex
    cache->cache_set = Calloc(n, sizeof(Cache_Pair));  //分配n个缓存对的空间
    cache->cache_pair_num = n;  //记录缓存对总容量
}

int cache_find(Cache *cache, Request *request, int fd) {
    Request_Line *request_line = &request->request_line;
    char match_str[MAXLINE];
    int cache_hit = 0;  //表示缓存是否命中
    Cache_Pair *cache_pair;
    sprintf(match_str, "%s http://%s:%s%s HTTP/1.0", request_line->method,
            request_line->host, request_line->port, request_line->path);
    P(&cache->mutex);  //加锁，防止其他线程读写
    for (int i = 0; i < cache->cache_pair_num; i++) {  //遍历缓存对
        cache_pair = cache->cache_set + i;
        if (!cache_pair->valid) {  //空缓存对，跳过
            continue;
        }
        if (!strcmp(cache_pair->request, match_str)) {  //匹配命中
            Rio_writen(fd, cache_pair->response,
                       MAX_OBJECT_SIZE);  //直接写入客户端连接
            cache_pair->time_stamp = cur_time_stamp++;  //更新时间戳
            cache_hit = 1;  //命中，更新cache_hit
            printf("Cache hit!\n");
            break;
        }
    }
    V(&cache->mutex);  //释放锁
    if (cache_hit) {
        printf("Cache hit: http://%s:%s%s\n\n", request_line->host,
               request_line->port, request_line->path);
    }
    return cache_hit;
}

void cache_insert(Cache *cache, Request *request, char *response) {
    Request_Line *request_line = &request->request_line;
    char match_str[MAXLINE];
    Cache_Pair *cache_pair, *oldest_pair;
    int oldest_time_stamp = 0x7fffffff;
    int vacancy = 0;  //表示是否有空位
    sprintf(match_str, "%s http://%s:%s%s HTTP/1.0", request_line->method,
            request_line->host, request_line->port, request_line->path);
    P(&cache->mutex);  //加锁，防止其他线程读写
    for (int i = 0; i < cache->cache_pair_num; i++) {  //遍历缓存对
        cache_pair = cache->cache_set + i;
        if (!cache_pair->valid) {  //如果有空位直接写进去
            strcpy(cache_pair->request, match_str);
            strcpy(cache_pair->response, response);
            cache_pair->valid = 1;
            cache_pair->time_stamp = cur_time_stamp++;
            vacancy = 1;
            break;
        }
        if (oldest_time_stamp >
            cache_pair->time_stamp) {  //查找最久未使用的缓存对
            oldest_time_stamp = cache_pair->time_stamp;
            oldest_pair = cache_pair;
        }
    }
    if (!vacancy) {  //如果没有空位，就去替换最久未使用的缓存对
        printf("Replace\n");
        strcpy(oldest_pair->request, match_str);
        strcpy(oldest_pair->response, response);
        oldest_pair->time_stamp = cur_time_stamp++;
    }
    V(&cache->mutex);  //解锁
}

/* Create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;                  /* Buffer holds max of n items */
    sp->front = sp->rear = 0;   /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0); /* Initially, buf has zero data items */
}

/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp) { Free(sp->buf); }

/* Insert item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);                          /* Wait for available slot */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->items);                          /* Announce available item */
}

/* Remove and return the first item from buffer sp */
int sbuf_remove(sbuf_t *sp) {
    int item;
    P(&sp->items);                           /* Wait for available item */
    P(&sp->mutex);                           /* Lock the buffer */
    item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
    V(&sp->mutex);                           /* Unlock the buffer */
    V(&sp->slots);                           /* Announce available slot */
    return item;
}

void read_request(int fd, Request *request) {
    char buf[MAXLINE];
    rio_t rio;
    Rio_readinitb(&rio, fd);
    read_request_line(&rio, buf, &request->request_line);  //读取请求行
    Rio_readlineb(&rio, buf, MAXLINE);
    Request_Header *header = request->request_headers;
    request->request_header_num = 0;
    while (strcmp(buf, "\r\n")) {  //循环读取请求头
        read_request_header(&rio, buf, header++);
        request->request_header_num++;
        Rio_readlineb(&rio, buf, MAXLINE);
    }
}

void read_request_line(rio_t *rp, char *buf, Request_Line *request_line) {
    char uri[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    printf("Request: %s\n", buf);
    if (sscanf(buf, "%s %s %s", request_line->method, uri,
               request_line->version) <
        3) {  //将请求行分割为三份，并检查分割结果
        fprintf(stderr, "Error: invalid request line!\n");
        exit(1);
    }
    if (strcasecmp(request_line->method, "GET")) {  //检查是否支持GET方法
        fprintf(stderr, "Method not implemented!\n");
        exit(1);
    }
    if (strcasecmp(request_line->version, "HTTP/1.0") &&
        strcasecmp(request_line->version, "HTTP/1.1")) {  //检查HTTP版本
        fprintf(stderr, "HTTP version not recognized!\n");
        exit(1);
    }
    parse_uri(uri, request_line->host, request_line->port,
              request_line->path);  //后续拆分URI
}

void read_request_header(rio_t *rp, char *buf, Request_Header *request_header) {
    Rio_readlineb(rp, buf, MAXLINE);
    char *c = strstr(buf, ": ");  //拆分成两部分
    if (!c) {                     //异常处理
        fprintf(stderr, "Error: invalid header: %s", buf);
        exit(1);
    }
    *c = '\0';
    strcpy(request_header->name, buf);  //存入结构体
    strcpy(request_header->value, c + 2);
}

void parse_uri(char *uri, char *host, char *port, char *path) {
    char *path_start, *port_start;
    if (strstr(uri, "http://") != uri) {
        fprintf(stderr, "Error: invalid uri!\n");
        exit(1);
    }
    uri += strlen("http://");
    if ((port_start = strstr(uri, ":")) == NULL) {
        strcpy(port, "80");
    } else {
        *port_start = '\0';
        strcpy(host, uri);
        uri = port_start + 1;
    }

    if ((path_start = strstr(uri, "/")) == NULL) {
        strcpy(path, "/");
    } else {
        strcpy(path, path_start);
        *path_start = '\0';
    }

    if (port_start) {
        strcpy(port, uri);
    } else {
        strcpy(host, uri);
    }
}

int send_request(Request *request) {
    int clientfd;
    char content[MAXLINE];
    Request_Line *request_line = &request->request_line;
    clientfd = Open_clientfd(request_line->host,
                             request_line->port);  //向目标服务器发起新连接
    rio_t rio;
    Rio_readinitb(&rio, clientfd);
    sprintf(content, "%s %s HTTP/1.0\r\n", request_line->method,
            request_line->path);  //请求行
    Rio_writen(clientfd, content, strlen(content));
    sprintf(content, "Host: %s:%s\r\n", request_line->host,
            request_line->port);  // Host请求头
    Rio_writen(clientfd, content, strlen(content));
    sprintf(content, "User-Agent: %s\r\n", user_agent_hdr);  // UA请求头
    Rio_writen(clientfd, content, strlen(content));
    sprintf(content, "Connection: close\r\n");  // Connection请求头
    Rio_writen(clientfd, content, strlen(content));
    sprintf(content, "Proxy-Connection: close\r\n");  // Proxy-Connection请求头
    Rio_writen(clientfd, content, strlen(content));
    for (int i = 0; i < request->request_header_num;
         i++) {  //读取其他无需修改的请求头并发送
        char *name = request->request_headers[i].name;
        char *value = request->request_headers[i].value;
        if (!strcasecmp(name, "Host") || !strcasecmp(name, "User-Agent") ||
            !strcasecmp(name, "Connection") ||
            !strcasecmp(name, "Proxy-Connection")) {
            continue;
        }
        sprintf(content, "%s%s: %s\r\n", content, name, value);
        Rio_writen(clientfd, content, strlen(content));
    }
    Rio_writen(clientfd, "\r\n",
               2 * sizeof(char));  // empty line terminates headers
    return clientfd;
}

void forward_response(int connfd, int targetfd, Request *request) {
    rio_t rio;
    int n;
    char buf[MAXLINE], content[MAX_OBJECT_SIZE];
    int response_bytes = 0;
    Rio_readinitb(&rio, targetfd);
    while (
        (n = Rio_readlineb(&rio, buf, MAXLINE))) {  //每次读取一行目标主机响应
        Rio_writen(connfd, buf, n);  //将该行写入和客户端的连接，转发响应
        if (response_bytes + n <= MAX_OBJECT_SIZE) {
            strcpy(content + response_bytes, buf);  //插入到末尾
        }
        response_bytes += n;  //更新总字节数量
    }
    if (response_bytes <= MAX_OBJECT_SIZE) {  //如果在允许大小内就插入缓存
        cache_insert(&cache, request, content);
    }
}

void doit(int fd) {
    Request request;
    read_request(fd, &request);  //读取客户端请求数据
    if (!cache_find(&cache, &request, fd)) {
        int clientfd = send_request(&request);  //向目标主机发起新连接
        forward_response(fd, clientfd, &request);  //将目标主机响应转发给客户端
        Close(clientfd);  //关闭和目标主机的连接
    }
}
