#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./CGImysql/sql_connection_pool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int setnonblocking(int fd);

//设置定时器相关参数
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

//信号处理函数
void sig_handler(int sig)
{
    printf("%s\n", "alarm信号处理");

    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;

    //将信号值从管道写端写入，传输字符类型，而非整型
    send(pipefd[1], (char *)&msg, 1, 0);

    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

void show_error(int connfd, const char *info)
{
    // printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

//设置信号的处理函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    //使被信号打断的系统调用自动重新发起
    if (restart)
        sa.sa_flags |= SA_RESTART;

    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);

    //执行sigaction函数（操作的信号，对信号设置新的处理方式）
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

//定时器回调函数
void cb_func(client_data *user_data)
{
    //删除非活动连接在socket上的注册事件，并关闭
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    // printf("%s%d\n", "超时, 关闭socket: ", user_data->sockfd);
    close(user_data->sockfd);

    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

//设置信号为LT阻塞模式
void addfd_lt(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

// int main(int argc, char *argv[])
int main()
{
    Log::get_instance()->init("./mylog.log", 8192, 2000000, 10); //异步日志模型

    int port = 8001;

    //忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }

    //创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance("localhost", "root", "1234", "myserver", 3306, 5);

    // http_conn数组；用户
    http_conn *users = new http_conn[MAX_FD];
    assert(users);

    //初始化数据库读取表
    users->initmysql_result();

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

    /* 用于存储epoll事件表中就绪事件的event数组 */
    epoll_event events[MAX_EVENT_NUMBER];
    /* 创建一个额外的文件描述符来唯一标识内核中的epoll事件表 */
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    /* 主线程往epoll内核事件表中注册监听socket事件，当listen到新的客户连接时，listenfd变为就绪事件 */
    // listenfd需要水平触发
    addfd_lt(epollfd, listenfd, false);
    //将上述epollfd赋值给http类的m_epollfd属性
    http_conn::m_epollfd = epollfd;

    //创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);

    //设置管道写端为非阻塞
    setnonblocking(pipefd[1]);
    //设置管道读端为ET非阻塞
    addfd(epollfd, pipefd[0], false);

    //传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    //由alarm或settimer设置的实施闹钟引起；终止进程
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    client_data *users_timer = new client_data[MAX_FD];

    //超时标志
    bool timeout = false;

    //每隔TIMESLOT时间触发SIGALRM信号
    alarm(TIMESLOT);

    printf("%s", "服务器启动......\n");

    bool stop_server = false;
    while (!stop_server)
    {
        // printf("%s", "epoll_wait等待中...\n");

        /* 主线程调用epoll_wait等待一组文件描述符上的事件，并将当前所有就绪的epoll_event复制到events数组中 */
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            Log::get_instance()->flush();
            break;
        }

        // printf("%s%d\n", "epoll_wait取得事件数: ", number);

        /* 遍历这一数组以处理这些已经就绪的事件 */
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd; // 事件表中就绪的socket文件描述符

            //处理新到的客户连接
            if (sockfd == listenfd)
            {
                // 当listen到新的用户连接，listenfd上则产生就绪事件
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength); //新socket

                // printf("%s%d\n", "产生了传输socket: ", connfd);

                if (connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    Log::get_instance()->flush();
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server is busy");
                    LOG_ERROR("%s", "Internal server busy");
                    Log::get_instance()->flush();
                    continue;
                }
                // http与socket一一对应，将新的socket加入epoll，应对后面的传输
                users[connfd].init(connfd, client_address);

                //初始化该连接对应的连接资源
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;

                //创建定时器临时变量
                util_timer *timer = new util_timer();
                //设置定时器对应的连接资源
                timer->user_data = &users_timer[connfd];
                //设置回调函数
                timer->cb_func = cb_func;

                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                //创建该连接对应的定时器，初始化为前述临时变量
                users_timer[connfd].timer = timer;
                //将该定时器添加到链表中
                timer_lst.add_timer(timer);
            }
            //处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // printf("%s\n", "异常事件！");

                //重复关闭？
                users[sockfd].close_conn();

                // 服务器关闭连接，移除对应的定时器
                cb_func(&users_timer[sockfd]);
                util_timer *timer = users_timer[sockfd].timer;
                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }
            //管道读端对应文件描述符发生读事件，处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                // printf("%s\n", "收到信号");

                int sig;
                char signals[1024];

                //从管道读端读出信号值，成功返回字节数，失败返回-1
                //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                // printf("%s\n", "处理客户连接上接收到的数据");

                //创建定时器临时变量，将该连接对应的定时器取出来
                util_timer *timer = users_timer[sockfd].timer;

                //读入对应缓冲区
                if (users[sockfd].read_once())
                {
                    // printf("%s\n", users[sockfd].m_read_buf);

                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    //处理读入的请求
                    pool->append(users + sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);

                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                    }
                }
                else
                {
                    // 服务器关闭连接
                    users[sockfd].close_conn();
                    //服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // printf("%s\n", "处理写事件");

                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    // users[sockfd].close_conn();

                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    users[sockfd].close_conn();

                    //服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        //处理定时器为非必须事件，收到信号并不是立马处理
        //完成读写事件后，再进行处理
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    //销毁数据库连接池
    connPool->DestroyPool();

    printf("%s\n", "服务器停止运行！");
    return 0;
}