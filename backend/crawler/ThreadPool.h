#pragma once

#include <functional>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t threadsNum);
    ~ThreadPool();

    void Enqueue(std::function<void()> task);
    // Tells main thread to wait until all tasks are finished before quitting the program
    static void Wait();

private:
    std::vector<std::thread> threads;
    std::mutex queueMutex;
    std::queue<std::function<void()>> tasks;
    std::condition_variable cv;

    bool stop = false;

    static inline std::atomic<int> activeTasks = 0;
    static inline std::condition_variable doneCV;
    static inline std::mutex doneMutex;
};
