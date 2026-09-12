#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace AndSwapChainHook::Input
{
enum class Action
{
    Down,
    PointerDown,
    Move,
    PointerUp,
    Up,
    Cancel,
    Reset
};
enum class Error
{
    None,
    InvalidArgument,
    WrongThread,
    UnexpectedSequence,
    StaleGeneration,
    Busy,
    Overflow
};
enum class Disposition
{
    Forward,
    Capture,
    Reject
};
struct Pointer
{
    std::uint8_t id = 0;
    float x = 0, y = 0;
};
struct Event
{
    std::uint64_t generation = 0;
    std::uint64_t timestampNanoseconds = 0;
    Action action = Action::Cancel;
    std::uint8_t pointerCount = 0;
    std::uint8_t changedIndex = 0;
    std::array<Pointer, 32> pointers{};
};
struct SubmitResult
{
    Error error = Error::None;
    Disposition disposition = Disposition::Reject;
    bool queued = false;
};
struct DrainResult
{
    Error error = Error::None;
    std::size_t count = 0;
};
struct Statistics
{
    std::uint64_t generation = 0, retiredGeneration = 0, overflows = 0, dropped = 0;
    std::uint32_t activePointers = 0, ownedPointers = 0;
    std::size_t queued = 0;
    bool captured = false, suppressed = false, pendingReset = false;
};
// One ordered physical gesture stream. Submit belongs to producer and Drain to
// consumer; ResetGeneration may run on the lifecycle owner. Methods synchronize.
// No Android event pointer or ImGui state crosses the queue. Event values are copied.
class Queue
{
  public:
    static constexpr std::size_t Capacity = 64;
    Queue(std::thread::id producer, std::thread::id consumer, std::uint64_t generation);
    // captureDown is consulted only for Down. All fingers inherit that decision.
    SubmitResult Submit(const Event &event, bool captureDown);
    // Action::Reset is synthetic: the consumer must release ALL held input state,
    // regardless of generation, before processing subsequent events. It is never
    // dropped and replaces queued events on overflow or generation invalidation.
    DrainResult Drain(std::span<Event> destination);
    // A retired physical gesture keeps its capture/forward decision until UP/CANCEL.
    // A second retiring active gesture returns Busy without changing generation.
    Error ResetGeneration(std::uint64_t generation);
    Statistics Stats() const;

  private:
    struct Gesture
    {
        std::uint64_t generation = 0;
        std::uint32_t mask = 0;
        bool capture = false, suppressed = false;
    };
    void ResetConsumer();
    mutable std::mutex mutex_;
    const std::thread::id producer_, consumer_;
    std::uint64_t generation_;
    std::array<Event, Capacity> events_{};
    std::size_t head_ = 0, size_ = 0;
    std::uint64_t overflows_ = 0, dropped_ = 0;
    bool resetPending_ = false;
    Gesture active_;
    std::optional<Gesture> retired_;
};
} // namespace AndSwapChainHook::Input
