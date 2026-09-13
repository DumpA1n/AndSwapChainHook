#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <sys/types.h>
#include <vector>

namespace AndSwapChainHook::Memory
{
enum class Error
{
    None,
    InvalidArgument,
    AddressOverflow,
    Unmapped,
    PermissionDenied,
    ShortRead,
    SystemError,
    UnsupportedPlatform,
    Cancelled,
    DeadlineExceeded,
    BudgetExceeded,
    IdentityChanged
};

struct ReadResult
{
    Error error = Error::None;
    std::size_t bytesRead = 0;
    int systemError = 0;
    explicit operator bool() const noexcept
    {
        return error == Error::None;
    }
};

class Reader
{
  public:
    virtual ~Reader() = default;
    // Implementations must never access bytes beyond the destination or report
    // more bytes than requested. Failed reads may initialize bytesRead bytes.
    virtual ReadResult Read(std::uintptr_t address, std::span<std::byte> destination) const = 0;
};

struct Mapping
{
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    bool readable = false;
};

enum class MappingReadPolicy
{
    RequireReadable,
    AllowProcfsForceRead
};

// Single owner: Open and RefreshMappings must not run concurrently with reads.
// The explicit PID overload opens a read-only procfs view. AllowProcfsForceRead
// permits pread on mapped pages without VM_READ; the kernel still owns access
// control and error reporting. Neither policy attaches or changes target state.
class ProcessReader final : public Reader
{
  public:
    ProcessReader() = default;
    ~ProcessReader() override;
    ProcessReader(const ProcessReader &) = delete;
    ProcessReader &operator=(const ProcessReader &) = delete;
    ReadResult Open();
    ReadResult Open(pid_t pid, MappingReadPolicy policy = MappingReadPolicy::RequireReadable);
    ReadResult Read(std::uintptr_t address, std::span<std::byte> destination) const override;
    ReadResult RefreshMappings();
    const std::vector<Mapping> &Mappings() const noexcept
    {
        return mappings_;
    }

  private:
    int fd_ = -1;
    pid_t pid_ = -1;
    MappingReadPolicy policy_ = MappingReadPolicy::RequireReadable;
    std::vector<Mapping> mappings_;
};

struct ModuleIdentity
{
    std::string path;
    std::string buildId;
    std::uintptr_t base = 0;
    std::size_t size = 0;
    // The loader must increment loadEpoch even when a reload reuses an address.
    std::uint64_t loadEpoch = 0;
    bool operator==(const ModuleIdentity &) const = default;
};

class ModuleCatalog
{
  public:
    // A replacement publishes a whole snapshot and invalidates cached generations.
    Error Replace(std::vector<ModuleIdentity> modules);
    std::uint64_t Generation() const noexcept
    {
        return generation_;
    }
    const std::vector<ModuleIdentity> &Modules() const noexcept
    {
        return modules_;
    }

  private:
    std::uint64_t generation_ = 0;
    std::vector<ModuleIdentity> modules_;
};

struct ScanOptions
{
    std::size_t byteBudget = 1024 * 1024;
    std::size_t maxMatches = 1024;
    std::size_t chunkSize = 4096;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    std::shared_ptr<const std::atomic<bool>> cancellation;
};
struct ScanResult
{
    Error error = Error::None;
    std::size_t bytesRead = 0;
    int systemError = 0;
    std::vector<std::uintptr_t> matches;
    explicit operator bool() const noexcept
    {
        return error == Error::None;
    }
};
// Exact byte patterns up to 65536 bytes, including matches crossing a read boundary. Partial
// matches and bytes from a short read are never treated as validated results.
ScanResult Scan(const Reader &reader, std::uintptr_t address, std::size_t length,
                std::span<const std::byte> pattern, const ScanOptions &options = {});
} // namespace AndSwapChainHook::Memory
