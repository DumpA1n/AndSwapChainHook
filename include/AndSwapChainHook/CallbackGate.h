#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace AndSwapChainHook::Instrumentation
{
// Publication and invocation share one mutex. User callbacks execute unlocked.
// Destruction requires all producers and callers to have been stopped by the host.
class CallbackGate
{
  public:
    using Callback = std::function<void()>;
    void Publish(Callback callback)
    {
        auto value = callback ? std::make_shared<const Callback>(std::move(callback)) : nullptr;
        std::shared_ptr<const Callback> retired;
        std::lock_guard lock(mutex_);
        retired = std::move(callback_);
        callback_ = std::move(value);
    }
    void Start()
    {
        std::lock_guard lock(mutex_);
        enabled_ = true;
    }
    bool Invoke()
    {
        std::shared_ptr<const Callback> callback;
        {
            std::lock_guard lock(mutex_);
            if (!enabled_ || !callback_)
                return false;
            callback = callback_;
            ++active_;
        }
        Invocation invocation{this, current_};
        current_ = &invocation;
        try
        {
            (*callback)();
        }
        catch (...)
        {
            callback.reset();
            Finish(invocation);
            throw;
        }
        callback.reset();
        Finish(invocation);
        return true;
    }
    // False means callback-initiated stop: publication is disabled immediately,
    // but an external owner must call Stop again to wait for all active callbacks.
    bool Stop()
    {
        std::shared_ptr<const Callback> retired;
        std::unique_lock lock(mutex_);
        enabled_ = false;
        retired = std::move(callback_);
        for (auto *call = current_; call; call = call->previous)
            if (call->owner == this)
                return false;
        drained_.wait(lock, [this] { return active_ == 0; });
        return true;
    }

  private:
    struct Invocation
    {
        CallbackGate *owner;
        Invocation *previous;
    };
    inline static thread_local Invocation *current_ = nullptr;
    void Finish(Invocation &invocation)
    {
        current_ = invocation.previous;
        std::lock_guard lock(mutex_);
        if (--active_ == 0)
            drained_.notify_all();
    }
    std::mutex mutex_;
    std::condition_variable drained_;
    std::shared_ptr<const Callback> callback_;
    std::size_t active_ = 0;
    bool enabled_ = false;
};
} // namespace AndSwapChainHook::Instrumentation
