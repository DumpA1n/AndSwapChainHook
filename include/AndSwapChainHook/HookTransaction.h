#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace AndSwapChainHook::Instrumentation
{
enum class HookError
{
    None,
    InvalidArgument,
    CapacityExceeded,
    DuplicateTarget,
    AlreadyInstalled,
    InstallationFailed,
    RestorationFailed,
    RollbackFailed,
    QuiescenceRequired,
    CallbacksPending,
    CallbackFailed,
    UnsupportedProvider,
    WrongThread
};
struct HookStatus
{
    HookError error = HookError::None;
    int backendError = 0;
    std::size_t index = 0;
    std::size_t retainedHooks = 0;
    explicit operator bool() const noexcept
    {
        return error == HookError::None;
    }
};
struct HookRequest
{
    void *target;
    void *replacement;
    void **original;
};
struct HookBackend
{
    void *context = nullptr;
    int (*install)(void *context, void *target, void *replacement, void **original) = nullptr;
    int (*restore)(void *context, void *target) = nullptr;
};
// Install requires all potential entry threads to be parked. Restore requires
// every acquired replacement/original trampoline address to be retired: no such
// address may execute during restoration or become callable after restoration.
// A backend can free trampolines on restore; parking alone is not sufficient.
// On failed installation, a backend sets a non-null original when restoration
// is still required; this record participates in rollback and remains on failure.
// A null original on failure means the target and its permissions are unchanged.
// A failed restoration retains both the record and its original function.
// No destructor restores executable code or claims safe DSO unloading.
class HookTransaction
{
  public:
    static constexpr std::size_t Capacity = 32;
    explicit HookTransaction(HookBackend backend) : backend_(backend) {}
    HookTransaction(const HookTransaction &) = delete;
    HookTransaction &operator=(const HookTransaction &) = delete;
    HookStatus Install(std::span<const HookRequest> requests, bool entriesQuiescent);
    HookStatus Restore(bool entryAddressesRetired);
    std::size_t Size() const noexcept
    {
        return count_;
    }

  private:
    HookBackend backend_;
    std::array<HookRequest, Capacity> records_{};
    std::size_t count_ = 0;
};
} // namespace AndSwapChainHook::Instrumentation
