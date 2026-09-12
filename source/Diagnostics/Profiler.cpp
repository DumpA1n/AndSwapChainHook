#include <AndSwapChainHook/Profiler.h>

#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace AndSwapChainHook::Profiler
{
namespace
{
struct Registry
{
    std::array<Zone, kMaxZones> zones;
    std::array<std::array<char, kMaxNameBytes + 1>, kMaxZones> names{};
    std::atomic<int> count{0};
    std::atomic<bool> enabled{false};
    std::mutex mutex;
};

Registry &Storage()
{
    static Registry value;
    return value;
}
} // namespace

Registration Register(std::string_view name)
{
    if (name.empty())
        return {Error::InvalidName, -1};
    if (name.size() > kMaxNameBytes)
        return {Error::NameTooLong, -1};
    if (name.find('\0') != std::string_view::npos)
        return {Error::InvalidName, -1};
    auto &registry = Storage();
    std::lock_guard lock(registry.mutex);
    const int index = registry.count.load(std::memory_order_relaxed);
    if (index == kMaxZones)
        return {Error::CapacityExhausted, -1};
    auto &owned = registry.names[index];
    std::memcpy(owned.data(), name.data(), name.size());
    owned[name.size()] = '\0';
    registry.zones[index].name = owned.data();
    registry.count.store(index + 1, std::memory_order_release);
    return {Error::None, index};
}

Registration Register(const char *name)
{
    if (!name)
        return {Error::InvalidName, -1};
    std::size_t length = 0;
    while (length <= kMaxNameBytes && name[length] != '\0')
        ++length;
    if (length > kMaxNameBytes)
        return {Error::NameTooLong, -1};
    return Register(std::string_view{name, length});
}

int ZoneCount() noexcept
{
    return Storage().count.load(std::memory_order_acquire);
}

const Zone *TryGetZone(int index) noexcept
{
    if (index < 0 || index >= ZoneCount())
        return nullptr;
    return &Storage().zones[index];
}

const Zone &GetZone(int index)
{
    const auto *zone = TryGetZone(index);
    if (!zone)
        throw std::out_of_range("Profiler zone index is not published");
    return *zone;
}

bool Record(int index, std::uint64_t nanoseconds) noexcept
{
    if (index < 0 || index >= ZoneCount())
        return false;
    Zone &zone = Storage().zones[index];
    zone.calls.fetch_add(1, std::memory_order_relaxed);
    zone.sumNs.fetch_add(nanoseconds, std::memory_order_relaxed);
    zone.lastNs.store(nanoseconds, std::memory_order_relaxed);
    auto minimum = zone.minNs.load(std::memory_order_relaxed);
    while (nanoseconds < minimum && !zone.minNs.compare_exchange_weak(
               minimum, nanoseconds, std::memory_order_relaxed)) {}
    auto maximum = zone.maxNs.load(std::memory_order_relaxed);
    while (nanoseconds > maximum && !zone.maxNs.compare_exchange_weak(
               maximum, nanoseconds, std::memory_order_relaxed)) {}
    const auto cursor = zone.histHead.fetch_add(1, std::memory_order_relaxed) % kHistoryLen;
    zone.histUs[cursor].store(static_cast<float>(nanoseconds) / 1000.0f, std::memory_order_relaxed);
    return true;
}

void SetEnabled(bool enabled) noexcept
{
    Storage().enabled.store(enabled, std::memory_order_relaxed);
}

bool IsEnabled() noexcept
{
    return Storage().enabled.load(std::memory_order_relaxed);
}

void Reset() noexcept
{
    const int count = ZoneCount();
    for (int index = 0; index < count; ++index)
    {
        auto &zone = Storage().zones[index];
        zone.calls.store(0, std::memory_order_relaxed);
        zone.sumNs.store(0, std::memory_order_relaxed);
        zone.minNs.store(UINT64_MAX, std::memory_order_relaxed);
        zone.maxNs.store(0, std::memory_order_relaxed);
        zone.lastNs.store(0, std::memory_order_relaxed);
    }
}

ScopedZone::ScopedZone(int index) noexcept : idx(index), active(IsEnabled() && TryGetZone(index))
{
    if (active)
        t0 = std::chrono::steady_clock::now();
}

ScopedZone::~ScopedZone()
{
    if (active)
    {
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count();
        Record(idx, static_cast<std::uint64_t>(duration));
    }
}
} // namespace AndSwapChainHook::Profiler
