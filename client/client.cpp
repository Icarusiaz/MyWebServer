#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include "../http/http_conn.h"

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
// extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

int main()
{
    int connfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET; //指定IP地址地址版本为IPV4
    // htonl：本机字节顺序转化为网络字节顺序，INADDR_ANY：将套接字绑定到所有IP,s_addr: int
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8080);
    connect(connfd, (struct sockaddr *)&address, sizeof(address));

    //读取服务器传回的数据
    char buffer[40];
    read(connfd, buffer, sizeof(buffer) - 1);

    printf("Message form server: %s\n", buffer);

    //关闭套接字
    close(connfd);

    return 0;
}