#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include "./http/http_conn.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
// extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// int main(int argc, char *argv[])
int main()
{
    // int port = atoi(argv[1]);
    int port = 8001;

    http_conn *users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    /**
     * @brief 创建监听socket文件描述符
     *     协议族为domain、协议类型为type、协议编号为protocol
     *     IPv4 Internet协议，TCP连接，协议中只有一种特定类型
     */
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    /* 创建监听socket的TCP/IP的IPV4 socket地址 */
    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET; //指定IP地址地址版本为IPV4
    // htonl：本机字节顺序转化为网络字节顺序，INADDR_ANY：将套接字绑定到所有IP,s_addr: int
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    /**
     * @brief Construct a new setsockopt object
     * SOL_SOCKET,基本套接口
     * SO_REUSEADDR 允许端口被重复使用,flag=1
     */
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    /* 绑定socket和它的地址 */
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    /* 创建监听队列以存放待处理的客户连接，在这些客户连接被accept()之前 ,5个*/
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    /* 创建一个额外的文件描述符来唯一标识内核中的epoll事件表 */
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    //将上述epollfd赋值给http类的m_epollfd属性
    http_conn::m_epollfd = epollfd;

    /* 用于存储epoll事件表中就绪事件的event数组 */
    epoll_event *events = new epoll_event[MAX_EVENT_NUMBER];
    /* 主线程往epoll内核事件表中注册监听socket事件，当listen到新的客户连接时，listenfd变为就绪事件 */
    addfd(epollfd, listenfd, false);

    printf("%s", "服务器启动......\n");

    bool stop_server = false;
    while (!stop_server)
    {
        printf("%s", "epoll_wait等待中...\n");

        /* 主线程调用epoll_wait等待一组文件描述符上的事件，并将当前所有就绪的epoll_event复制到events数组中 */
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            break;
        }

        printf("%s", "epoll_wait取得事件\n");

        /* 遍历这一数组以处理这些已经就绪的事件 */
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd; // 事件表中就绪的socket文件描述符

            if (sockfd == listenfd)
            {
                // 当listen到新的用户连接，listenfd上则产生就绪事件
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength); //新socket

                printf("%s%d\n", "产生了传输socket\t", connfd);

                if (connfd < 0)
                {
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server is busy");
                    continue;
                }
                // http与socket一一对应，将新的socket加入epoll，应对后面的传输
                users[connfd].init(connfd, client_address);

                continue;
            }
            //处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器关闭连接
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                //读入对应缓冲区
                if (users[sockfd].read_once())
                {
                    //处理读入的请求
                    std::cout << users[sockfd].m_read_buf << std::endl;
                    users[sockfd].process();
                }
                else
                {
                    // 服务器关闭连接
                }
            }
            else if (events[i].events & EPOLLOUT)
            {

                printf("%s\n", "处理写事件");
                if (!users[sockfd].write())
                    users[sockfd].close_conn();
            }
        }
    }

    return 0;
}