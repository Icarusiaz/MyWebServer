#pragma once
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <queue>
#include <stdarg.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../log/log.h"

#define BUFFER_SIZE 64

class util_timer;

struct client_data
{
    sockaddr_in address; //客户端socket地址
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer *timer;
};

//定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;                  //超时时间
    void (*cb_func)(client_data *); //回调函数
    client_data *user_data;         //连接资源
    util_timer *prev;
    util_timer *next;
};

class sort_timer_lst
{
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst()
    {
        util_timer *tmp = head;
        while (tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    //添加定时器，内部调用私有成员add_timer
    void add_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }
        if (!head)
        {
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }

    //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }

        util_timer *tmp = timer->next;

        //被调整的定时器在链表尾部
        //定时器超时值仍然小于下一个定时器超时值，不调整
        if (!tmp || (timer->expire < tmp->expire))
        {
            return;
        }

        //被调整定时器是链表头结点，将定时器取出，重新插入
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        //被调整定时器在内部，将定时器取出，重新插入
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    //删除定时器
    void del_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }

        //链表中只有一个定时器，需要删除该定时器
        if ((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }

        //被删除的定时器为头结点
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }

        //被删除的定时器为尾结点
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }

        //被删除的定时器在链表内部，常规链表结点删除
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    //定时任务处理函数
    void tick()
    {
        if (!head)
        {
            return;
        }

        LOG_INFO("%s", "timer tick");
        Log::get_instance()->flush();

        //获取当前时间
        time_t cur = time(NULL);

        util_timer *tmp = head;
        //遍历定时器链表
        while (tmp)
        {
            //链表容器为升序排列
            //当前时间小于定时器的超时时间，后面的定时器也没有到期
            if (cur < tmp->expire)
            {
                break;
            }

            //当前定时器到期，则调用回调函数，执行定时事件
            tmp->cb_func(tmp->user_data);

            //将处理后的定时器从链表容器中删除，并重置头结点
            head = tmp->next;
            if (head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        while (tmp)
        {
            if (timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    util_timer *head;
    util_timer *tail;
};

/**********************************TODO 时间堆****************************************/
