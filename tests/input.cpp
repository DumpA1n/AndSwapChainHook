#include <AndSwapChainHook/Input.h>
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value "\n";                                             \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
using namespace AndSwapChainHook::Input;
Event Make(Action action, std::uint64_t generation = 1)
{
    return {.generation = generation,
            .action = action,
            .pointerCount = 1,
            .pointers = {Pointer{2, 10, 10}}};
}
int main()
{
    const auto self = std::this_thread::get_id();
    Queue queue(self, self, 1);
    auto down = Make(Action::Down);
    CHECK(queue.Submit(down, true).queued);
    auto second = Make(Action::PointerDown);
    second.pointerCount = 2;
    second.changedIndex = 1;
    second.pointers[1] = {7, 500, 500};
    CHECK(queue.Submit(second, false).disposition == Disposition::Capture);
    CHECK(queue.Stats().ownedPointers == ((1u << 2) | (1u << 7)));
    second.action = Action::PointerUp;
    second.changedIndex = 0;
    CHECK(queue.Submit(second, false).queued);
    auto up = Make(Action::Up);
    up.pointers[0].id = 7;
    CHECK(queue.Submit(up, false).disposition == Disposition::Capture &&
          queue.Stats().activePointers == 0);
    Event output[Queue::Capacity];
    auto drained = queue.Drain(output);
    CHECK(drained.count == 4 && output[0].action == Action::Down && output[3].action == Action::Up);
    CHECK(queue.Submit(down, false).disposition == Disposition::Forward);
    CHECK(queue.Submit(Make(Action::Move), true).disposition == Disposition::Forward);
    CHECK(queue.Submit(Make(Action::Up), true).disposition == Disposition::Forward);
    CHECK(queue.Drain(output).count == 0);
    CHECK(queue.Submit(down, true).queued);
    for (std::size_t i = 1; i < Queue::Capacity; ++i)
        CHECK(queue.Submit(Make(Action::Move), false).queued);
    CHECK(queue.Submit(Make(Action::Move), false).error == Error::Overflow);
    CHECK(queue.Stats().overflows == 1 && queue.Stats().pendingReset);
    CHECK(queue.Submit(Make(Action::Up), false).disposition == Disposition::Capture);
    CHECK(queue.Submit(down, true).queued);
    CHECK(queue.Drain(output).count == 2 && output[0].action == Action::Reset &&
          output[1].action == Action::Down);
    CHECK(queue.ResetGeneration(2) == Error::None);
    CHECK(queue.Submit(Make(Action::Up), false).disposition == Disposition::Capture);
    CHECK(queue.Submit(Make(Action::Down, 2), false).disposition == Disposition::Forward);
    CHECK(queue.Submit(Make(Action::Up, 1), false).error == Error::StaleGeneration);
    CHECK(queue.Stats().activePointers == (1u << 2));
    CHECK(queue.ResetGeneration(3) == Error::None);
    CHECK(queue.Submit(Make(Action::Down, 3), true).queued);
    CHECK(queue.ResetGeneration(4) == Error::Busy);
    CHECK(queue.Submit(Make(Action::Up, 2), true).disposition == Disposition::Forward);
    CHECK(queue.Submit(Make(Action::Cancel, 3), false).disposition == Disposition::Capture);
    CHECK(queue.ResetGeneration(4) == Error::None);
    CHECK(queue.Drain(output).count == 1 && output[0].action == Action::Reset);
    std::thread wrong(
        [&]
        {
            CHECK(queue.Submit(Make(Action::Down, 4), true).error == Error::WrongThread);
            CHECK(queue.Drain(output).error == Error::WrongThread);
        });
    wrong.join();
    auto invalid = Make(Action::Down, 4);
    invalid.pointers[0].id = 32;
    CHECK(queue.Submit(invalid, true).error == Error::InvalidArgument);
    CHECK(queue.Submit(Make(Action::Up, 4), true).error == Error::UnexpectedSequence);
    std::promise<std::thread::id> consumerId;
    std::atomic<bool> ready{false}, finished{false};
    std::unique_ptr<Queue> concurrent;
    std::thread consumer(
        [&]
        {
            consumerId.set_value(std::this_thread::get_id());
            while (!ready.load(std::memory_order_acquire))
                std::this_thread::yield();
            bool held = false;
            for (;;)
            {
                Event batch[8];
                const auto result = concurrent->Drain(batch);
                CHECK(result.error == Error::None);
                for (std::size_t i = 0; i < result.count; ++i)
                {
                    if (batch[i].action == Action::Reset)
                        held = false;
                    else if (batch[i].action == Action::Down)
                    {
                        CHECK(!held);
                        held = true;
                    }
                    else if (batch[i].action == Action::Up)
                    {
                        CHECK(held);
                        held = false;
                    }
                }
                if (finished.load(std::memory_order_acquire) && result.count == 0)
                    break;
                std::this_thread::yield();
            }
            CHECK(!held);
        });
    concurrent = std::make_unique<Queue>(self, consumerId.get_future().get(), 1);
    ready.store(true, std::memory_order_release);
    for (int i = 0; i < 10000; ++i)
    {
        CHECK(concurrent->Submit(Make(Action::Down), true).disposition == Disposition::Capture);
        CHECK(concurrent->Submit(Make(Action::Up), true).disposition == Disposition::Capture);
    }
    finished.store(true, std::memory_order_release);
    consumer.join();
    std::cout << "Input value queue: ownership, multiple pointers, saturation/reset, generation, "
                 "late UP and thread roles passed\n";
}
