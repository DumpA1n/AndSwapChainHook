#include <AndSwapChainHook/Profiler.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define CHECK(value)                                                                                \
    do                                                                                              \
    {                                                                                               \
        if (!(value))                                                                               \
        {                                                                                           \
            std::cerr << __LINE__ << ": " #value "\n";                                               \
            std::abort();                                                                           \
        }                                                                                           \
    } while (false)

namespace Prof = AndSwapChainHook::Profiler;

void ValidateNames()
{
    const int count = Prof::ZoneCount();
    CHECK(count >= 0 && count <= Prof::kMaxZones);
    for (int index = 0; index < count; ++index)
    {
        const auto &zone = Prof::GetZone(index);
        CHECK(zone.name && std::strlen(zone.name) <= Prof::kMaxNameBytes);
        if (index == 0)
            CHECK(std::strcmp(zone.name, std::string(Prof::kMaxNameBytes, 'N').c_str()) == 0);
        else
            CHECK(std::strncmp(zone.name, "writer-", 7) == 0);
    }
}

int main()
{
    CHECK(Prof::ZoneCount() == 0 && !Prof::IsEnabled());
    CHECK(Prof::Register(nullptr).error == Prof::Error::InvalidName);
    CHECK(Prof::Register("").error == Prof::Error::InvalidName);
    CHECK(Prof::Register(std::string_view{"a\0b", 3}).error == Prof::Error::InvalidName);
    const std::string longName(Prof::kMaxNameBytes + 1, 'x');
    CHECK(Prof::Register(longName).error == Prof::Error::NameTooLong);
    CHECK(Prof::Register(longName.c_str()).error == Prof::Error::NameTooLong);
    std::array<char, Prof::kMaxNameBytes + 1> unterminated{};
    unterminated.fill('x');
    CHECK(Prof::Register(unterminated.data()).error == Prof::Error::NameTooLong);
    CHECK(Prof::ZoneCount() == 0);
    const auto owned = Prof::Register(std::string(Prof::kMaxNameBytes, 'N'));
    CHECK(owned && owned.index == 0);
    CHECK(Prof::TryGetZone(-1) == nullptr && Prof::TryGetZone(1) == nullptr);
    bool invalidIndexThrew = false;
    try { Prof::GetZone(Prof::kMaxZones); }
    catch (const std::out_of_range &) { invalidIndexThrew = true; }
    CHECK(invalidIndexThrew);
    CHECK(!Prof::Record(-1, 1) && !Prof::Record(Prof::kMaxZones, 1));

    std::atomic<bool> done{false};
    std::atomic<int> observedCount{0};
    std::thread reader([&]
    {
        while (!done.load(std::memory_order_acquire))
        {
            ValidateNames();
            observedCount.store(Prof::ZoneCount(), std::memory_order_release);
        }
        ValidateNames();
    });
    std::atomic<int> successes{0}, full{0};
    std::vector<std::thread> writers;
    constexpr int threads = 8;
    constexpr int attempts = 40;
    for (int thread = 0; thread < threads; ++thread)
        writers.emplace_back([&, thread]
        {
            for (int attempt = 0; attempt < attempts; ++attempt)
            {
                std::string name = "writer-" + std::to_string(thread) + "-" + std::to_string(attempt);
                const auto result = Prof::Register(name.c_str());
                name.assign(name.size(), '!');
                if (result)
                {
                    successes.fetch_add(1);
                    while (observedCount.load(std::memory_order_acquire) <= result.index)
                        std::this_thread::yield();
                }
                else
                {
                    CHECK(result.error == Prof::Error::CapacityExhausted && result.index == -1);
                    full.fetch_add(1);
                }
            }
        });
    for (auto &writer : writers)
        writer.join();
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(successes == Prof::kMaxZones - 1);
    CHECK(full == threads * attempts - successes);
    CHECK(observedCount == Prof::kMaxZones && Prof::ZoneCount() == Prof::kMaxZones);
    CHECK(Prof::Register("capacity").error == Prof::Error::CapacityExhausted);

    const auto &zone = Prof::GetZone(owned.index);
    { Prof::ScopedZone disabled(owned.index); }
    CHECK(zone.calls == 0);
    Prof::SetEnabled(true);
    { Prof::ScopedZone enabled(owned.index); }
    CHECK(zone.calls == 1);
    Prof::SetEnabled(false);
    for (std::uint64_t sample = 1; sample <= 256; ++sample)
        CHECK(Prof::Record(owned.index, sample * 1000));
    CHECK(zone.calls == 257 && zone.lastNs == 256000);
    const auto cursor = zone.histHead.load();
    std::array<float, Prof::kHistoryLen> history{};
    for (int index = 0; index < Prof::kHistoryLen; ++index)
    {
        history[index] = zone.histUs[index].load();
        CHECK(history[index] >= 129.0f && history[index] <= 256.0f);
    }
    Prof::Reset();
    CHECK(zone.calls == 0 && zone.sumNs == 0 && zone.minNs == UINT64_MAX &&
          zone.maxNs == 0 && zone.lastNs == 0 && zone.histHead == cursor);
    for (int index = 0; index < Prof::kHistoryLen; ++index)
        CHECK(zone.histUs[index] == history[index]);

    writers.clear();
    constexpr int samples = 10000;
    for (int thread = 0; thread < threads; ++thread)
        writers.emplace_back([&, thread]
        {
            for (int sample = 0; sample < samples; ++sample)
                CHECK(Prof::Record(owned.index, thread + 1));
        });
    for (auto &writer : writers)
        writer.join();
    CHECK(zone.calls == threads * samples);
    CHECK(zone.sumNs == samples * threads * (threads + 1) / 2);
    CHECK(zone.minNs == 1 && zone.maxNs == threads);
    CHECK(zone.lastNs >= 1 && zone.lastNs <= threads);
    CHECK(zone.histHead == cursor + threads * samples);
    ValidateNames();
    std::cout << "PASS: owned bounded names, concurrent publication/capacity, 80000 concurrent "
                 "records, scoped timing and retained history\n";
}
