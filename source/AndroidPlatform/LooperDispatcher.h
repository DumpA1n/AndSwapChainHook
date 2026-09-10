#pragma once
#include <android/looper.h>

#include <functional>
#include <mutex>
#include <queue>

class LooperDispatcher
{
public:
    LooperDispatcher() = default;
    ~LooperDispatcher();

    LooperDispatcher(const LooperDispatcher &) = delete;
    LooperDispatcher &operator=(const LooperDispatcher &) = delete;

    // Call on the thread whose ALooper will own the dispatcher.
    void init();

    // Post a task from any thread to the dispatcher thread.
    void post(std::function<void()> task);

    void cleanup();

private:
    static int looper_callback(int fd, int events, void* data);

    int efd_ = -1;
    ALooper *looper_ = nullptr;
    std::mutex mtx_;
    std::queue<std::function<void()>> tasks_;
    bool initialized_ = false;
};
