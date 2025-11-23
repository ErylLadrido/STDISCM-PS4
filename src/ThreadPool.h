#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();
    
    template<class F>
    void enqueue(F&& task);
    
    void waitAll();
    
private:
    void workerThread();
    
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop;
    std::atomic<int> m_activeTasks;
    std::condition_variable m_completionCondition;
};

template<class F>
void ThreadPool::enqueue(F&& task) {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_tasks.emplace(std::forward<F>(task));
        m_activeTasks++;
    }
    m_condition.notify_one();
}

#endif // THREADPOOL_H