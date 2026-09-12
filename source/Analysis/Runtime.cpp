#include <AndSwapChainHook/Memory.h>
#include <AndSwapChainHook/Runtime.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace Memory = AndSwapChainHook::Memory;
struct ASC_Session
{
    std::mutex operation;
    std::mutex publication;
    Memory::ProcessReader reader;
    std::shared_ptr<std::atomic<bool>> cancellation = std::make_shared<std::atomic<bool>>(false);
    std::thread worker;
    ASC_Status status{ASC_OK, ASC_CREATED, 0, 0, 0, 0};
    std::shared_ptr<const Memory::ScanResult> snapshot;
    bool initialized = false;
};
namespace
{
ASC_Status Invalid()
{
    return {ASC_INVALID_ARGUMENT, ASC_CREATED, 0, 0, 0, 0};
}
ASC_Status Snapshot(ASC_Session *session)
{
    std::lock_guard lock(session->publication);
    return session->status;
}
ASC_Status Error(ASC_Session *session, ASC_Error error)
{
    auto result = Snapshot(session);
    result.error = error;
    return result;
}
} // namespace
ASC_Status ASC_Create(ASC_Session **session)
{
    if (!session)
        return Invalid();
    *session = nullptr;
    try
    {
        *session = new ASC_Session;
        return Snapshot(*session);
    }
    catch (...)
    {
        return {ASC_INTERNAL_ERROR, ASC_CREATED, 0, 0, 0, 0};
    }
}
ASC_Status ASC_Initialize(ASC_Session *session)
{
    if (!session)
        return Invalid();
    std::lock_guard operation(session->operation);
    if (Snapshot(session).state == ASC_RUNNING)
        return Error(session, ASC_BUSY);
    try
    {
        auto result = session->reader.Open();
        std::lock_guard publication(session->publication);
        if (!result)
        {
            session->status = {ASC_MEMORY_ERROR,   ASC_FAILED, static_cast<int>(result.error),
                               result.systemError, 0,          0};
        }
        else
        {
            session->initialized = true;
            session->snapshot.reset();
            session->status = {ASC_OK, ASC_INITIALIZED, 0, 0, 0, 0};
        }
        return session->status;
    }
    catch (...)
    {
        return Error(session, ASC_INTERNAL_ERROR);
    }
}
ASC_Status ASC_Start(ASC_Session *session, uintptr_t address, size_t length,
                     const unsigned char *pattern, size_t patternLength, ASC_ScanOptions options)
{
    if (!session || !pattern || !patternLength || !options.byteBudget || !options.chunkSize ||
        !options.maxMatches || !options.deadlineMilliseconds || patternLength > 65536 ||
        patternLength > options.byteBudget)
        return Invalid();
    std::lock_guard operation(session->operation);
    if (!session->initialized)
        return Error(session, ASC_INVALID_STATE);
    if (Snapshot(session).state == ASC_RUNNING)
        return Error(session, ASC_BUSY);
    try
    {
        if (session->worker.joinable())
            session->worker.join();
        auto mapping = session->reader.RefreshMappings();
        if (!mapping)
        {
            auto result = Error(session, ASC_MEMORY_ERROR);
            result.memoryError = static_cast<int>(mapping.error);
            result.systemError = mapping.systemError;
            return result;
        }
        const auto *begin = reinterpret_cast<const std::byte *>(pattern);
        std::vector<std::byte> copiedPattern(begin, begin + patternLength);
        session->cancellation->store(false, std::memory_order_release);
        {
            std::lock_guard publication(session->publication);
            session->snapshot.reset();
            session->status = {ASC_OK, ASC_RUNNING, 0, 0, 0, 0};
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(options.deadlineMilliseconds);
        session->worker = std::thread(
            [session, address, length, pattern = std::move(copiedPattern), options, deadline]
            {
                try
                {
                    Memory::ScanOptions scanOptions{options.byteBudget, options.maxMatches,
                                                    options.chunkSize, deadline,
                                                    session->cancellation};
                    auto result = std::make_shared<const Memory::ScanResult>(
                        Memory::Scan(session->reader, address, length, pattern, scanOptions));
                    std::lock_guard publication(session->publication);
                    session->snapshot = result;
                    session->status = {*result ? ASC_OK : ASC_MEMORY_ERROR,
                                       *result                                     ? ASC_COMPLETED
                                       : result->error == Memory::Error::Cancelled ? ASC_CANCELLED
                                                                                   : ASC_FAILED,
                                       static_cast<int>(result->error),
                                       result->systemError,
                                       result->bytesRead,
                                       result->matches.size()};
                }
                catch (...)
                {
                    std::lock_guard publication(session->publication);
                    session->status = {ASC_INTERNAL_ERROR, ASC_FAILED, 0, 0, 0, 0};
                }
            });
        return Snapshot(session);
    }
    catch (...)
    {
        std::lock_guard publication(session->publication);
        session->status = {ASC_INTERNAL_ERROR, ASC_FAILED, 0, 0, 0, 0};
        return session->status;
    }
}
ASC_Status ASC_Cancel(ASC_Session *session)
{
    if (!session)
        return Invalid();
    session->cancellation->store(true, std::memory_order_release);
    return Snapshot(session);
}
ASC_Status ASC_Stop(ASC_Session *session)
{
    if (!session)
        return Invalid();
    std::lock_guard operation(session->operation);
    session->cancellation->store(true, std::memory_order_release);
    if (session->worker.joinable())
        session->worker.join();
    std::lock_guard publication(session->publication);
    session->status.state = ASC_STOPPED;
    return session->status;
}
ASC_Status ASC_Snapshot(ASC_Session *session, uintptr_t *matches, size_t capacity)
{
    if (!session || (!matches && capacity))
        return Invalid();
    std::lock_guard publication(session->publication);
    auto status = session->status;
    if (!matches)
        return status;
    if (status.matchCount > capacity)
    {
        status.error = ASC_CAPACITY_EXCEEDED;
        return status;
    }
    if (session->snapshot)
        std::copy(session->snapshot->matches.begin(), session->snapshot->matches.end(), matches);
    return status;
}
void ASC_Destroy(ASC_Session *session)
{
    if (session)
    {
        ASC_Stop(session);
        delete session;
    }
}
