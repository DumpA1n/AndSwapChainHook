#include <AndSwapChainHook/HookTransaction.h>

namespace AndSwapChainHook::Instrumentation
{
HookStatus HookTransaction::Install(std::span<const HookRequest> requests, bool entriesQuiescent)
{
    if (!entriesQuiescent)
        return {HookError::QuiescenceRequired, 0, 0, count_};
    if (count_)
        return {HookError::AlreadyInstalled, 0, 0, count_};
    if (!backend_.install || !backend_.restore || requests.empty())
        return {HookError::InvalidArgument};
    if (requests.size() > Capacity)
        return {HookError::CapacityExceeded};
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        const auto &request = requests[i];
        if (!request.target || !request.replacement || !request.original)
            return {HookError::InvalidArgument, 0, i};
        for (std::size_t j = 0; j < i; ++j)
            if (requests[j].target == request.target || requests[j].original == request.original)
                return {HookError::DuplicateTarget, 0, i};
    }
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        const auto &request = requests[i];
        *request.original = nullptr;
        const int error = backend_.install(backend_.context, request.target, request.replacement,
                                           request.original);
        if (error)
        {
            if (*request.original)
                records_[count_++] = request;
            const auto rollback = Restore(true);
            return {rollback ? HookError::InstallationFailed : HookError::RollbackFailed, error, i,
                    count_};
        }
        records_[count_++] = request;
    }
    return {HookError::None, 0, 0, count_};
}
HookStatus HookTransaction::Restore(bool entryAddressesRetired)
{
    if (!entryAddressesRetired)
        return {HookError::QuiescenceRequired, 0, 0, count_};
    HookStatus result;
    for (std::size_t i = count_; i > 0; --i)
    {
        const auto &record = records_[i - 1];
        const int error = backend_.restore(backend_.context, record.target);
        if (error)
        {
            if (result)
                result = {HookError::RestorationFailed, error, i - 1};
            continue;
        }
        for (std::size_t j = i; j < count_; ++j)
            records_[j - 1] = records_[j];
        --count_;
    }
    result.retainedHooks = count_;
    return result;
}
} // namespace AndSwapChainHook::Instrumentation
