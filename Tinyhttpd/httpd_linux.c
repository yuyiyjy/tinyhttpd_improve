/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>                                 // socket网络编程核心头文件
#include <sys/types.h>                                  // 系统数据结构
#include <netinet/in.h>                                 // 网络地址结构体
#include <arpa/inet.h>                                  // IP地址转换函数
#include <unistd.h>                                     // UNIX标准函数（close, fork等）
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>                                   // 文件状态
//#include <pthread.h>             
#include <sys/wait.h>                                   // 进程等待(waitpid)
#include <stdlib.h>
#include <stdint.h>

// 宏定义
#define ISspace(x) isspace((int)(x))                    // 判断字符是否为空格 用于解析HTTP请求时跳过空白字符

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"      // 服务器标识，在HTTP响应头中发送给客户端
// 标准文件描述符定义（用于CGI执行时的输入输出重定向）
#define STDIN   0                                       // 标准输入文件描述符
#define STDOUT  1                                       // 标准输出文件描述符
#define STDERR  2                                       // 标准错误文件描述符

// 函数声明
void accept_request(void *);                                          // 处理客户端请求的主函数
void bad_request(int);                                                // 返回404错误
void cat(int, FILE *);                                                // 将文件内容发送到客户端
void cannot_execute(int);                                             // 返回CGI执行错误
void error_die(const char *);                                         // 错误处理函数
void execute_cgi(int, const char *, const char *, const char *);      // 执行CGI程序
int get_line(int, char *, int);                                       // 从socket读取一行数据
void headers(int, const char *);                                      // 发送HTTP响应头
void not_found(int);                                                  // 返回404错误
void serve_file(int, const char *);                                   // 处理静态文件请求
int startup(u_short *);                                               // 启动HTTP服务器，完成socket创建、端口绑定和监听
void unimplemented(int);                                              // 返回未实现的方法错误

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/

/* 函数功能：处理客户端HTTP请求的核心函数
 * 参数：客户端socket*/

void accept_request(void *arg)
{
    // =========== 1.变量初始化 ===========
    int client = (intptr_t)arg;             // 客户端socket 从main函数传递过来
    char buf[1024];                         // 缓冲区 存储读取的数据
    size_t numchars;                        // 实际读取的字符数 
    char method[255];                      // HTTP方法 存储GET或POST
    char url[255];                          // URL 存储请求的URL
    char path[512];                         // 路径 存储文件路径
    size_t i, j;                            // 循环变量
    struct stat st;                         // 文件状态结构体
    int cgi = 0;      /* becomes true if server decides this is a CGI   // 是否为CGI请求的标志
                       * program */
    char *query_string = NULL;              // 查询字符串 存储GET请求的参数

    // =========== 2.读取请求行并解析HTTP方法 ===========
    numchars = get_line(client, buf, sizeof(buf));                 // 从socket读取一行数据
    i = 0; j = 0;
    // 解析HTTP方法，读取字符直到遇到空格
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;                                       // 记录HTTP方法的结束位置
    method[i] = '\0';                          // 结束HTTP方法字符串

    // 检查HTTP方法是否为GET或POST，否则返回未实现的方法错误
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    // 如果HTTP方法为POST，则一定是CGI程序，需要处理请求体数据
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    // =========== 3.读取请求行并解析URL ===========
    i = 0;
    // 跳过方法后面的空格
    while (ISspace(buf[j]) && (j < numchars))
        j++;

    // 解析URL，读取字符直到遇到空格或换行符
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';                          // 结束URL字符串

    // =========== 4.GET请求处理查询字符串 ===========
    // 检查是否有查询字符串（？后面的部分）
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;                   // 查询字符串指向URL

        // 查找？字符的位置
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;

        // 找到了？说明有查询参数
        if (*query_string == '?')
        {
            cgi = 1;                          // 一定是CGI程序
            *query_string = '\0';             // 结束查询字符串
            query_string++;                   // 跳过？ 指针移到参数部分
        }
    }

    // =========== 5.构建文件路径并检查 ===========
    sprintf(path, "htdocs%s", url);          // 构建文件路径

    // 如果url以/结尾，默认添加index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    // 检查文件是否存在 stat函数用于获取文件状态，如果文件不存在则返回-1
    if (stat(path, &st) == -1) {
        // 若文件不存在，则丢弃所有请求头，返回404错误
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        // 如果是目录，默认访问index.html st.st_mode & S_IFMT提取文件类型
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");

        // 判断是否为CGI程序
        if ((st.st_mode & S_IXUSR) ||                 // 所有者可执行
                (st.st_mode & S_IXGRP) ||             // 组用户可执行
                (st.st_mode & S_IXOTH)    )           // 其他人可执行
            cgi = 1;

        // 根据CGI标志决定处理方式
        if (!cgi)
            serve_file(client, path);             // 不是CGI则处理静态文件请求
        else
            execute_cgi(client, path, method, query_string);  // 是CGI则执行CGI程序
    }

    close(client);              // 关闭客户端socket
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* 函数功能：将文件内容发送到客户端（类似于UNIX的cat命令）
 * 参数：client - 客户端socket描述符
 *       resource - 要发送的文件指针
 * 实现方式：循环读取文件 → 发送到socket，直到文件结束
 **********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];                                    // 文件读取缓冲区

    // =========== 循环读取并发送文件内容 ===========
    fgets(buf, sizeof(buf), resource);                 // 读取第一行（最多1023字符）
    while (!feof(resource))                            // 循环直到文件末尾
    {
        send(client, buf, strlen(buf), 0);             // 将读取的内容发送到客户端
        fgets(buf, sizeof(buf), resource);             // 继续读取下一行
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
        const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
    {
    }


    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path,path,(char *) NULL);                // 在原代码的基础上修正execl函数的调用
        exit(0);
    } else {    /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* 函数功能：发送HTTP响应头（200 OK状态）
 * 参数：client - 客户端socket描述符
 *       filename - 文件名（本实现中未使用，可用于判断Content-Type）
 * HTTP响应头格式：
 *   HTTP/1.0 200 OK\r\n
 *   Server: jdbhttpd/0.1.0\r\n
 *   Content-Type: text/html\r\n
 *   \r\n  （空行表示头部结束）
 **********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;                                    // 故意不用filename，避免编译器警告
                                                       // 实际可根据扩展名设置Content-Type（如.jpg→image/jpeg）

    // =========== 1.发送状态行 ===========
    strcpy(buf, "HTTP/1.0 200 OK\r\n");               // HTTP/1.0 200 OK 表示请求成功处理
    send(client, buf, strlen(buf), 0);

    // =========== 2.发送服务器标识 ===========
    strcpy(buf, SERVER_STRING);                        // Server: jdbhttpd/0.1.0\r\n
    send(client, buf, strlen(buf), 0);

    // =========== 3.发送内容类型 ===========
    sprintf(buf, "Content-Type: text/html\r\n");       // 固定返回text/html，可优化为根据文件类型判断
    send(client, buf, strlen(buf), 0);

    // =========== 4.发送空行表示头部结束 ===========
    strcpy(buf, "\r\n");                               // 空行是HTTP头部和响应体的分隔符
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 函数功能：返回404 Not Found错误页面
 * 参数：client - 客户端socket描述符
 * 返回内容：HTTP 404状态 + HTML错误页面
 **********************************************************************/
void not_found(int client)
{
    char buf[1024];

    // =========== 1.发送HTTP响应头 ===========
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");       // 状态行：404表示资源未找到
    send(client, buf, strlen(buf), 0);

    sprintf(buf, SERVER_STRING);                       // 服务器标识
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-Type: text/html\r\n");       // 内容类型为HTML
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "\r\n");                              // 空行分隔头部和响应体
    send(client, buf, strlen(buf), 0);

    // =========== 2.发送HTML错误页面内容 ===========
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 函数功能：处理静态文件请求，发送文件内容给客户端
 * 参数：client - 客户端socket描述符
 *       filename - 要发送的文件路径
 * 执行流程：
 *   1. 丢弃所有请求头（HTTP协议要求）
 *   2. 打开文件，失败返回404
 *   3. 发送HTTP 200响应头
 *   4. 发送文件内容
 *   5. 关闭文件
 **********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;                             // 文件指针
    int numchars = 1;
    char buf[1024];

    // =========== 1.丢弃所有请求头 ===========
    // HTTP请求格式：请求行 + 请求头 + 空行 + 请求体
    // 对于GET请求静态文件，请求头信息（User-Agent、Cookie等）不需要处理
    // 但必须读完，因为TCP是流式协议，不读完会影响后续读取
    buf[0] = 'A'; buf[1] = '\0';                       // 初始化buf，确保第一次strcmp不为0，进入循环
    while ((numchars > 0) && strcmp("\n", buf))        // 循环读取直到空行（只有\n的行表示头部结束）
        numchars = get_line(client, buf, sizeof(buf));

    // =========== 2.打开文件 ===========
    resource = fopen(filename, "r");                   // 以只读方式打开文件
    if (resource == NULL)
        not_found(client);                             // 文件不存在，返回404错误
    else
    {
        // =========== 3.发送HTTP响应头 ===========
        headers(client, filename);                     // 发送200 OK状态 + 响应头

        // =========== 4.发送文件内容 ===========
        cat(client, resource);                         // 循环读取文件并发送到客户端
    }

    // =========== 5.关闭文件 ===========
    fclose(resource);                                  // 关闭文件释放资源
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/

/*函数功能：启动HTTP服务器，完成socket创建、端口绑定和监听
  参数：指向端口号的指针，如果传入0则动态分配一个端口
  返回值：监听socket*/
int startup(u_short *port)
{
    // =============== 1.变量定义 ===============
    int httpd = 0;                    // 监听socket描述符
    int on = 1;                       // socket选项标志
    struct sockaddr_in name;          // 服务器地址结构体

    // =============== 2.创建socket ===============
    // PF_INET：使用IPv4协议族
    // SOCK_STREAM：使用TCP流式套接字（面向连接、可靠传输）
    // 0：使用默认的协议（TCP）
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");                       // 创建失败则报错退出

    // =============== 3.初始化服务器地址结构 ===============
    memset(&name, 0, sizeof(name));                // 将服务器地址结构体清零，避免垃圾数据
    name.sin_family = AF_INET;                     // 使用IPv4地址族
    name.sin_port = htons(*port);                  // 将端口号转换为网络字节序（大端）
    name.sin_addr.s_addr = htonl(INADDR_ANY);      // IP地址：监听所有网卡 0.0.0.0

    // =============== 4.设置socket选项 地址复用(SO_REUSEADDR) 防止端口占用===============
    // 作用：允许快速重启服务器，避免端口占用
    // 当服务器异常退出后，端口可能处于TIME_WAIT状态，导致无法重新绑定，设置SO_REUSEADDR选项可以避免此问题
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }

    // =============== 5.绑定socket ===============
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");

    // =============== 6.动态端口分配处理 ===============
    // 如果传入的端口为0，系统会自动分配一个可用端口
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)         // 通过getsockname获取实际分配的端口号，并通过指针返回给调用者
            error_die("getsockname");
        *port = ntohs(name.sin_port);           // 将端口号转换为主机字节序（小端）
    }

    // =============== 7.开始监听连接 ===============
    if (listen(httpd, 5) < 0)             // 开始监听连接，参数5表示请求队列的最大长度
        error_die("listen");              // 当多个客户端同时连接时，超过5个的会被拒绝或等待
    return(httpd);                        // 返回监听socket，供后续accept使用
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 函数功能：主函数 HTTP服务器入口 */
int main(void)
{
    //                =============== 1.定义变量 ===============
    int server_sock = -1;                            // 服务器监听socket
    u_short port = 4000;                             // 监听端口号
    int client_sock = -1;                            // 客户端连接socket
    struct sockaddr_in client_name;                  // 客户端地址信息
    socklen_t  client_name_len = sizeof(client_name);  // 获取客户端地址信息长度
    // pthread_t newthread;                          // 为运行而注释掉

    // =============== 2.启动服务器（创建socket，绑定端口，开始监听）===============
    server_sock = startup(&port);                     // 调用startup函数启动服务器，传入端口号
    printf("httpd running on port %d\n", port);

    // =============== 3.等待客户端连接并处理请求 ===============
    while (1)
    {
        // accept()阻塞调用，没有客户端连接时程序暂定在这里
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");                         // 接受连接失败则报错
        // accept_request(&client_sock);                 // 原代码中的accept_request函数调用存在问题，参数类型不匹配

        // 处理客户端请求（单进程串行处理），(intptr_t) 强制转换是为了消除指针和整数之间的警告
        accept_request((void *)(intptr_t)client_sock);  
        /*if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");*/               // 为运行而注释掉
    }

    // =============== 4.关闭服务器socket并退出 ===============
    close(server_sock);

    return(0);
}
