#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t threadsNum) {
    // Create worker threads
    for (size_t i = 0; i < threadsNum; ++i) {
        threads.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(queueMutex);

                    cv.wait(lock, [this] {
                        return !tasks.empty() || stop;
                    });

                    if (stop && tasks.empty())
                        return;

                    task = std::move(tasks.front());
                    tasks.pop();
                }

                task();
                if (ThreadPool::activeTasks.fetch_sub(1) == 1)
                    doneCV.notify_one();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }

    cv.notify_all();

    // Join all worker threads to ensure they have completed their tasks
    for (auto& thread : threads) {
        thread.join();
    }
}

void ThreadPool::Enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.emplace(std::move(task));
        ThreadPool::activeTasks.fetch_add(1);
    }

    cv.notify_one();
}

void ThreadPool::Wait() {
    std::unique_lock<std::mutex> lock(doneMutex);
    doneCV.wait(lock, [] {
        return activeTasks.load() == 0;
    });
}
