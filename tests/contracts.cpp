#include <AndSwapChainHook/CallbackGate.h>
#include <AndSwapChainHook/HookTransaction.h>
#include <AndSwapChainHook/Memory.h>
#include <AndSwapChainHook/Runtime.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #value "\n";                          \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
using namespace AndSwapChainHook;

struct FixtureReader final : Memory::Reader
{
    std::vector<std::byte> bytes;
    mutable std::size_t calls = 0;
    mutable std::size_t lastCount = 0;
    Memory::Error error = Memory::Error::None;
    bool shortRead = false;
    Memory::ReadResult Read(std::uintptr_t address, std::span<std::byte> output) const override
    {
        ++calls;
        lastCount = output.size();
        if (error != Memory::Error::None)
            return {error};
        if (address < 0x1000 || address - 0x1000 > bytes.size() ||
            output.size() > bytes.size() - (address - 0x1000))
            return {Memory::Error::Unmapped};
        const auto count = output.size() - (shortRead && !output.empty() ? 1 : 0);
        std::copy_n(bytes.begin() + address - 0x1000, count, output.begin());
        return {shortRead ? Memory::Error::ShortRead : Memory::Error::None, count};
    }
};
void MemoryContracts()
{
    FixtureReader reader;
    reader.bytes = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{1},
                    std::byte{2}, std::byte{3}, std::byte{9}};
    const std::byte pattern[] = {std::byte{1}, std::byte{2}, std::byte{3}};
    Memory::ScanOptions options;
    options.chunkSize = 2;
    auto result = Memory::Scan(reader, 0x1000, reader.bytes.size(), pattern, options);
    CHECK(result && result.matches == std::vector<std::uintptr_t>({0x1000, 0x1003}));
    CHECK(reader.lastCount == 1 && result.bytesRead == 7);
    options.byteBudget = 6;
    CHECK(Memory::Scan(reader, 0x1000, 7, pattern, options).error == Memory::Error::BudgetExceeded);
    options.byteBudget = 64;
    options.maxMatches = 1;
    CHECK(Memory::Scan(reader, 0x1000, 7, pattern, options).error == Memory::Error::BudgetExceeded);
    options.maxMatches = 16;
    options.deadline = std::chrono::steady_clock::now();
    const auto calls = reader.calls;
    CHECK(Memory::Scan(reader, 0x1000, 7, pattern, options).error ==
          Memory::Error::DeadlineExceeded);
    CHECK(reader.calls == calls);
    options.deadline = std::chrono::steady_clock::time_point::max();
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    options.cancellation = cancellation;
    cancellation->store(true);
    CHECK(Memory::Scan(reader, 0x1000, 7, pattern, options).error == Memory::Error::Cancelled);
    options.cancellation = {};
    reader.shortRead = true;
    result = Memory::Scan(reader, 0x1000, 7, pattern, options);
    CHECK(result.error == Memory::Error::ShortRead && result.matches.empty() &&
          result.bytesRead == 1);
    reader.shortRead = false;
    reader.error = Memory::Error::PermissionDenied;
    CHECK(Memory::Scan(reader, 0x1000, 7, pattern, options).error ==
          Memory::Error::PermissionDenied);
    CHECK(Memory::Scan(reader, std::numeric_limits<std::uintptr_t>::max(), 2, pattern, options)
              .error == Memory::Error::AddressOverflow);
    reader.error = Memory::Error::None;
    reader.bytes[1] = std::byte{9};
    CHECK(Memory::Scan(reader, 0x1000, 7, pattern, options).matches ==
          std::vector<std::uintptr_t>({0x1003}));
    Memory::ModuleCatalog catalog;
    std::vector<Memory::ModuleIdentity> modules{{"fixture", "build-id", 0x1000, 7, 1}};
    CHECK(catalog.Replace(modules) == Memory::Error::None && catalog.Generation() == 1);
    CHECK(catalog.Replace(modules) == Memory::Error::None && catalog.Generation() == 1);
    modules[0].loadEpoch = 2;
    CHECK(catalog.Replace(modules) == Memory::Error::None && catalog.Generation() == 2);
    modules[0].size = 0;
    CHECK(catalog.Replace(modules) == Memory::Error::InvalidArgument && catalog.Generation() == 2);
#if defined(__linux__)
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void *mapping =
        mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    *static_cast<unsigned char *>(mapping) = 42;
    Memory::ProcessReader invalidProcess;
    CHECK(invalidProcess.Open(0).error == Memory::Error::InvalidArgument);
    Memory::ProcessReader process;
    CHECK(process.Open(getpid()));
    CHECK(process.Open());
    CHECK(mprotect(static_cast<char *>(mapping) + page, page, PROT_READ) == 0);
    CHECK(process.RefreshMappings());
    std::byte crossing[16]{};
    CHECK(process.Read(reinterpret_cast<std::uintptr_t>(mapping) + page - 8, crossing));
    std::byte byte{};
    CHECK(process.Read(reinterpret_cast<std::uintptr_t>(mapping), std::span(&byte, 1)) &&
          byte == std::byte{42});
    CHECK(mprotect(mapping, page, PROT_NONE) == 0);
    CHECK(process.RefreshMappings());
    CHECK(process.Read(reinterpret_cast<std::uintptr_t>(mapping), std::span(&byte, 1)).error ==
          Memory::Error::PermissionDenied);
    CHECK(munmap(mapping, page * 2) == 0);
    CHECK(process.RefreshMappings());
    CHECK(process.Read(reinterpret_cast<std::uintptr_t>(mapping), std::span(&byte, 1)).error ==
          Memory::Error::Unmapped);
#endif
}
struct FakeBackend
{
    int installCalls = 0;
    int failInstall = 0;
    bool failAfterInstallation = false;
    void *failRestore = nullptr;
    std::vector<void *> installed;
    std::vector<void *> restored;
    Instrumentation::HookBackend Backend()
    {
        return {this,
                [](void *context, void *target, void *, void **original)
                {
                    auto &self = *static_cast<FakeBackend *>(context);
                    if (++self.installCalls == self.failInstall && !self.failAfterInstallation)
                        return -7;
                    self.installed.push_back(target);
                    *original = target;
                    return self.installCalls == self.failInstall ? -7 : 0;
                },
                [](void *context, void *target)
                {
                    auto &self = *static_cast<FakeBackend *>(context);
                    self.restored.push_back(target);
                    if (target == self.failRestore)
                        return -8;
                    self.installed.erase(
                        std::find(self.installed.begin(), self.installed.end(), target));
                    return 0;
                }};
    }
};
void HookContracts()
{
    using namespace Instrumentation;
    int targets[3]{};
    void *originals[3]{};
    HookRequest requests[3];
    for (int i = 0; i < 3; ++i)
        requests[i] = {&targets[i], &requests, &originals[i]};
    for (int failure = 1; failure <= 3; ++failure)
    {
        FakeBackend backend;
        backend.failInstall = failure;
        HookTransaction transaction(backend.Backend());
        auto result = transaction.Install(requests, true);
        CHECK(result.error == HookError::InstallationFailed && result.backendError == -7);
        CHECK(transaction.Size() == 0 && backend.installed.empty());
        for (std::size_t i = 0; i < backend.restored.size(); ++i)
            CHECK(backend.restored[i] == &targets[failure - 2 - i]);
    }
    FakeBackend partial;
    partial.failInstall = 2;
    partial.failAfterInstallation = true;
    partial.failRestore = &targets[1];
    HookTransaction partialTransaction(partial.Backend());
    const auto partialResult = partialTransaction.Install(requests, true);
    CHECK(partialResult.error == HookError::RollbackFailed && partialResult.retainedHooks == 1);
    CHECK(partial.installed == std::vector<void *>{&targets[1]} && originals[1] == &targets[1]);
    partial.failRestore = nullptr;
    CHECK(partialTransaction.Restore(true) && partial.installed.empty());
    FakeBackend backend;
    HookTransaction transaction(backend.Backend());
    std::array<HookRequest, HookTransaction::Capacity + 1> excessive{};
    CHECK(transaction.Install(excessive, true).error == HookError::CapacityExceeded);
    CHECK(backend.installCalls == 0);
    CHECK(transaction.Install(requests, false).error == HookError::QuiescenceRequired &&
          backend.installCalls == 0);
    CHECK(transaction.Install(requests, true));
    CHECK(transaction.Install(requests, true).error == HookError::AlreadyInstalled);
    CHECK(transaction.Restore(false).error == HookError::QuiescenceRequired);
    backend.failRestore = &targets[1];
    auto result = transaction.Restore(true);
    CHECK(result.error == HookError::RestorationFailed && result.retainedHooks == 1 &&
          backend.installed.size() == 1);
    CHECK(originals[0] == &targets[0] && originals[1] == &targets[1]);
    CHECK(backend.restored == std::vector<void *>({&targets[2], &targets[1], &targets[0]}));
    backend.failRestore = nullptr;
    CHECK(transaction.Restore(true) && transaction.Size() == 0 && backend.installed.empty());
    CHECK(transaction.Restore(true));
    FakeBackend failedRollback;
    failedRollback.failInstall = 3;
    failedRollback.failRestore = &targets[0];
    HookTransaction rollback(failedRollback.Backend());
    CHECK(rollback.Install(requests, true).error == HookError::RollbackFailed &&
          rollback.Size() == 1);
    failedRollback.failRestore = nullptr;
    CHECK(rollback.Restore(true));
    requests[1].target = requests[0].target;
    CHECK(transaction.Install(requests, true).error == HookError::DuplicateTarget);
}
void CallbackContracts()
{
    Instrumentation::CallbackGate gate;
    CHECK(!gate.Invoke());
    std::atomic<int> calls = 0;
    gate.Publish(
        [&]
        {
            ++calls;
            CHECK(!gate.Stop());
        });
    gate.Start();
    CHECK(gate.Invoke() && calls == 1 && !gate.Invoke() && gate.Stop());
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, release = false;
    gate.Publish(
        [&]
        {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
        });
    gate.Start();
    std::thread worker([&] { CHECK(gate.Invoke()); });
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return entered; });
    }
    std::atomic<bool> stopped = false;
    std::thread stopper(
        [&]
        {
            CHECK(gate.Stop());
            stopped = true;
        });
    // An active callback owns its immutable function while publication changes.
    for (int i = 0; i < 1000; ++i)
        gate.Publish([&] { ++calls; });
    {
        std::lock_guard lock(mutex);
        CHECK(!stopped);
        release = true;
        condition.notify_all();
    }
    worker.join();
    stopper.join();
    CHECK(stopped && !gate.Invoke());
    gate.Publish([] { throw 1; });
    gate.Start();
    try
    {
        gate.Invoke();
        CHECK(false);
    }
    catch (int)
    {
    }
    CHECK(gate.Stop());
    struct ReentrantCapture
    {
        Instrumentation::CallbackGate &gate;
        ~ReentrantCapture()
        {
            gate.Publish({});
        }
    };
    auto capture = std::make_shared<ReentrantCapture>(gate);
    gate.Publish([capture] {});
    capture.reset();
    gate.Publish([] {});
    CHECK(gate.Stop());
}
void SessionContracts()
{
    ASC_Session *session = nullptr;
    CHECK(ASC_Create(&session).error == ASC_OK && session);
    CHECK(ASC_Snapshot(session, nullptr, 0).state == ASC_CREATED);
    const unsigned char bytes[] = {1, 2, 3, 4, 1, 2, 3, 4};
    const unsigned char pattern[] = {1, 2};
    const ASC_ScanOptions options{1024, 16, 2, 1000};
    CHECK(ASC_Start(session, reinterpret_cast<std::uintptr_t>(bytes), sizeof(bytes), pattern,
                    sizeof(pattern), options)
              .error == ASC_INVALID_STATE);
#if defined(__linux__)
    CHECK(ASC_Initialize(session).error == ASC_OK);
    for (int cycle = 0; cycle < 20; ++cycle)
    {
        CHECK(ASC_Start(session, reinterpret_cast<std::uintptr_t>(bytes), sizeof(bytes), pattern,
                        sizeof(pattern), options)
                  .error == ASC_OK);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        ASC_Status result;
        do
        {
            result = ASC_Snapshot(session, nullptr, 0);
            std::this_thread::yield();
        } while (result.state == ASC_RUNNING && std::chrono::steady_clock::now() < deadline);
        CHECK(result.state == ASC_COMPLETED && result.matchCount == 2 && result.bytesRead == 8);
        uintptr_t matches[2]{};
        CHECK(ASC_Snapshot(session, matches, 1).error == ASC_CAPACITY_EXCEEDED);
        CHECK(ASC_Snapshot(session, matches, 2).error == ASC_OK);
        CHECK(matches[0] == reinterpret_cast<std::uintptr_t>(bytes) &&
              matches[1] == reinterpret_cast<std::uintptr_t>(bytes + 4));
        CHECK(ASC_Stop(session).state == ASC_STOPPED);
        CHECK(ASC_Snapshot(session, nullptr, 0).matchCount == 2);
        CHECK(ASC_Initialize(session).error == ASC_OK);
        CHECK(ASC_Snapshot(session, matches, 1).matchCount == 0);
    }
#else
    CHECK(ASC_Initialize(session).memoryError ==
          static_cast<int>(Memory::Error::UnsupportedPlatform));
#endif
    ASC_Destroy(session);
}
int main()
{
    MemoryContracts();
    HookContracts();
    CallbackContracts();
    SessionContracts();
    std::cout << "R01/R05 memory, lifecycle, failure-injection contracts passed\n";
}
