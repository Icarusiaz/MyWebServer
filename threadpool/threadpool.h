#pragma once
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

template <typename T>
class threadpool
{
public:
    // thread_number是线程池中线程的数量
    // max_requests是请求队列中最多允许的、等待处理的请求的数量
    threadpool(int thread_number = 4, int max_request = 10000);
    ~threadpool();

    //向请求队列中插入任务请求
    bool append(T *request);

private:
    //工作线程运行的函数
    //它不断从工作队列中取出任务并执行之
    static void *worker(void *arg);
    void run();

private:
    //线程池中的线程数
    int m_thread_number;

    //请求队列中允许的最大请求数
    int m_max_requests;

    //描述线程池的数组，其大小为m_thread_number
    pthread_t *m_threads;

    //请求队列
    std::list<T *> m_workqueue;

    //保护请求队列的互斥锁
    locker m_queuelocker;

    //是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_request) : m_thread_number(thread_number), m_max_requests(max_request), m_stop(false), m_threads(NULL)
{
    if (thread_number <= 0 || max_request <= 0)
        throw std::exception();

    //线程id初始化
    m_threads = new pthread_t[thread_number];
    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_number; i++)
    {
        //循环创建线程，并将工作线程按要求进行运行
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }

        //将线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T *request)
{
    m_queuelocker.lock();

    //根据硬件，预先设置请求队列的最大值
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    //添加任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();

    //信号量提醒有任务要处理
    m_queuestat.post();
    return true;
}

//参数传入的是threadpool对象
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    //将参数强转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    //线程不终止
    while (!m_stop)
    {
        //信号量等待
        m_queuestat.wait();

        //被唤醒后先加互斥锁
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        //从请求队列中取出第一个任务
        //将任务从请求队列删除
        T *request = m_workqueue.front();
        m_workqueue.pop_front();

        m_queuelocker.unlock();
        if (!request)
            continue;

        request->process();
    }
}