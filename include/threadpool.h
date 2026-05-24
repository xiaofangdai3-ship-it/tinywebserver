#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>

template<typename T>
class ThreadPool {
public:
    ThreadPool(size_t threads = 8);
    ~ThreadPool();

    bool append(T* request);

    void worker();

    void shutdown();

private:
    std::vector<std::thread> workers;
    std::queue<T*> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

template<typename T>
ThreadPool<T>::ThreadPool(size_t threads) : stop(false) {
    for(size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while(true) {
                T* request;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });

                    if(this->stop && this->tasks.empty()) {
                        return;
                    }

                    request = this->tasks.front();
                    this->tasks.pop();
                }
                request->process();
            }
        });
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    shutdown();
}

template<typename T>
bool ThreadPool<T>::append(T* request) {
    std::unique_lock<std::mutex> lock(queue_mutex);
    if(stop) {
        return false;
    }
    tasks.push(request);
    condition.notify_one();
    return true;
}

template<typename T>
void ThreadPool<T>::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker : workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

#endif
