#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace AndSwapChainHook::Profiler
{
inline constexpr int kMaxZones = 96;
inline constexpr int kHistoryLen = 128;
inline constexpr std::size_t kMaxNameBytes = 95;

enum class Error
{
    None,
    InvalidName,
    NameTooLong,
    CapacityExhausted
};

struct Registration
{
    Error error = Error::InvalidName;
    int index = -1;
    explicit operator bool() const noexcept { return error == Error::None; }
};

struct Zone
{
    // The registry owns this immutable, terminated name until component destruction.
    const char *name = nullptr;
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> sumNs{0};
    std::atomic<std::uint64_t> minNs{UINT64_MAX};
    std::atomic<std::uint64_t> maxNs{0};
    std::atomic<std::uint64_t> lastNs{0};
    std::atomic<float> histUs[kHistoryLen]{};
    std::atomic<std::uint32_t> histHead{0};
};

// Names are copied before publication; indices are never reused or deduplicated.
// Empty names and embedded zero bytes are invalid. Names exceeding kMaxNameBytes
// are rejected without truncation. The C-string overload reads at most 96 bytes.
Registration Register(std::string_view name);
Registration Register(const char *name);
int ZoneCount() noexcept;
const Zone *TryGetZone(int index) noexcept;
// An unpublished or invalid index throws std::out_of_range.
const Zone &GetZone(int index);

// Counters and history elements are atomic, not a coherent multi-field snapshot.
// Concurrent writers do not provide chronological ordering of history samples.
// Integer counters use unsigned modular arithmetic. Invalid indices return false.
bool Record(int index, std::uint64_t nanoseconds) noexcept;
void SetEnabled(bool enabled) noexcept;
bool IsEnabled() noexcept;
// Resets counters individually, retaining names, history, and the history cursor.
// Stop recording first when the caller needs a clean measurement interval.
void Reset() noexcept;

struct ScopedZone
{
    int idx;
    bool active;
    std::chrono::steady_clock::time_point t0;

    explicit ScopedZone(int index) noexcept;
    ~ScopedZone();
    ScopedZone(const ScopedZone &) = delete;
    ScopedZone &operator=(const ScopedZone &) = delete;
};
} // namespace AndSwapChainHook::Profiler

#define ANDSWAPCHAIN_PROF_CONCAT_(a, b) a##b
#define ANDSWAPCHAIN_PROF_CONCAT(a, b) ANDSWAPCHAIN_PROF_CONCAT_(a, b)
// Each expansion evaluates and copies its name once, including when timing is disabled.
#define ANDSWAPCHAIN_PROF_ZONE(name) ANDSWAPCHAIN_PROF_ZONE_IMPL(name, __COUNTER__)
#define ANDSWAPCHAIN_PROF_ZONE_IMPL(name, counter)                                           \
    static const int ANDSWAPCHAIN_PROF_CONCAT(_asc_prof_index_, counter) =                    \
        ::AndSwapChainHook::Profiler::Register(name).index;                                  \
    ::AndSwapChainHook::Profiler::ScopedZone ANDSWAPCHAIN_PROF_CONCAT(_asc_prof_zone_, counter)( \
        ANDSWAPCHAIN_PROF_CONCAT(_asc_prof_index_, counter))
