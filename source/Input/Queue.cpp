#include <AndSwapChainHook/Input.h>
#include <cmath>

namespace AndSwapChainHook::Input
{
namespace
{
std::optional<std::uint32_t> PointerMask(const Event &event)
{
    switch (event.action)
    {
    case Action::Down:
    case Action::PointerDown:
    case Action::Move:
    case Action::PointerUp:
    case Action::Up:
    case Action::Cancel:
        break;
    default:
        return std::nullopt;
    }
    if (event.pointerCount > 32 || (event.pointerCount && event.changedIndex >= event.pointerCount))
        return std::nullopt;
    if (event.action != Action::Cancel && !event.pointerCount)
        return std::nullopt;
    std::uint32_t mask = 0;
    for (std::size_t i = 0; i < event.pointerCount; ++i)
    {
        const auto &pointer = event.pointers[i];
        if (pointer.id >= 32 || !std::isfinite(pointer.x) || !std::isfinite(pointer.y))
            return std::nullopt;
        const auto bit = std::uint32_t{1} << pointer.id;
        if (mask & bit)
            return std::nullopt;
        mask |= bit;
    }
    return mask;
}
bool Ends(Action action)
{
    return action == Action::Up || action == Action::Cancel;
}
} // namespace
Queue::Queue(std::thread::id producer, std::thread::id consumer, std::uint64_t generation)
    : producer_(producer), consumer_(consumer), generation_(generation)
{
}
void Queue::ResetConsumer()
{
    dropped_ += size_;
    head_ = size_ = 0;
    resetPending_ = true;
}
SubmitResult Queue::Submit(const Event &event, bool captureDown)
{
    std::lock_guard lock(mutex_);
    if (std::this_thread::get_id() != producer_)
        return {Error::WrongThread};
    if (!generation_ || !event.generation || consumer_ == std::thread::id{})
        return {Error::InvalidArgument};
    const auto mask = PointerMask(event);
    if (!mask)
        return {Error::InvalidArgument};
    if (retired_ && event.generation == retired_->generation)
    {
        if (event.action == Action::Down)
            return {Error::StaleGeneration};
        const auto disposition = retired_->capture ? Disposition::Capture : Disposition::Forward;
        if (Ends(event.action))
            retired_.reset();
        return {Error::None, disposition, false};
    }
    if (event.generation != generation_)
        return {Error::StaleGeneration};
    const auto changed =
        event.pointerCount ? std::uint32_t{1} << event.pointers[event.changedIndex].id : 0;
    if (event.action == Action::Down)
    {
        if (active_.mask || event.pointerCount != 1)
            return {Error::UnexpectedSequence};
        active_ = {generation_, *mask, captureDown, false};
    }
    else
    {
        if (!active_.mask)
            return {Error::UnexpectedSequence};
        if (event.action != Action::Cancel)
        {
            if (event.action == Action::PointerDown)
            {
                if ((*mask & ~changed) != active_.mask || (active_.mask & changed))
                    return {Error::UnexpectedSequence};
                active_.mask = *mask;
            }
            else if (*mask != active_.mask)
                return {Error::UnexpectedSequence};
            if (event.action == Action::PointerUp && event.pointerCount < 2)
                return {Error::UnexpectedSequence};
            if (event.action == Action::Up && event.pointerCount != 1)
                return {Error::UnexpectedSequence};
        }
    }
    const auto disposition = active_.capture ? Disposition::Capture : Disposition::Forward;
    const bool suppressed = active_.suppressed;
    if (event.action == Action::PointerUp)
        active_.mask &= ~changed;
    bool queued = false;
    Error error = Error::None;
    if (active_.capture && !suppressed)
    {
        if (size_ == Capacity)
        {
            ++overflows_;
            ++dropped_;
            ResetConsumer();
            active_.suppressed = true;
            error = Error::Overflow;
        }
        else
        {
            events_[(head_ + size_) % Capacity] = event;
            ++size_;
            queued = true;
        }
    }
    if (Ends(event.action))
        active_ = {};
    return {error, disposition, queued};
}
DrainResult Queue::Drain(std::span<Event> destination)
{
    std::lock_guard lock(mutex_);
    if (std::this_thread::get_id() != consumer_)
        return {Error::WrongThread};
    if (destination.empty())
        return {};
    std::size_t count = 0;
    if (resetPending_)
    {
        destination[count++] = Event{.generation = generation_, .action = Action::Reset};
        resetPending_ = false;
    }
    while (count < destination.size() && size_)
    {
        destination[count++] = events_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
    }
    return {Error::None, count};
}
Error Queue::ResetGeneration(std::uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (!generation || generation <= generation_)
        return Error::InvalidArgument;
    if (active_.mask && retired_)
        return Error::Busy;
    if (active_.mask)
    {
        retired_ = active_;
        retired_->suppressed = retired_->capture;
        active_ = {};
    }
    generation_ = generation;
    ResetConsumer();
    return Error::None;
}
Statistics Queue::Stats() const
{
    std::lock_guard lock(mutex_);
    return {generation_,
            retired_ ? retired_->generation : 0,
            overflows_,
            dropped_,
            active_.mask,
            active_.capture ? active_.mask : 0,
            size_,
            active_.capture,
            active_.suppressed,
            resetPending_};
}
} // namespace AndSwapChainHook::Input
