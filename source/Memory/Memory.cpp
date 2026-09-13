#include <AndSwapChainHook/Memory.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#if defined(__linux__)
#include <fcntl.h>
#include <string>
#include <unistd.h>
#endif

namespace AndSwapChainHook::Memory
{
namespace
{
bool Overflows(std::uintptr_t address, std::size_t size)
{
    return size > std::numeric_limits<std::uintptr_t>::max() - address;
}
Error SystemError(int error)
{
    return error == EACCES || error == EPERM ? Error::PermissionDenied : Error::SystemError;
}
} // namespace
ProcessReader::~ProcessReader()
{
#if defined(__linux__)
    if (fd_ >= 0)
        close(fd_);
#endif
}
ReadResult ProcessReader::Open()
{
#if defined(__linux__)
    return Open(getpid(), MappingReadPolicy::RequireReadable);
#else
    return {Error::UnsupportedPlatform};
#endif
}
ReadResult ProcessReader::Open(pid_t pid, MappingReadPolicy policy)
{
#if defined(__linux__)
    if (fd_ >= 0)
        return pid_ == pid && policy_ == policy ? ReadResult{} : ReadResult{Error::InvalidArgument};
    if (pid <= 0)
        return {Error::InvalidArgument};
    const std::string path = "/proc/" + std::to_string(pid) + "/mem";
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
        return {SystemError(errno), 0, errno};
    pid_ = pid;
    policy_ = policy;
    auto result = RefreshMappings();
    if (!result)
    {
        close(descriptor);
        pid_ = -1;
        policy_ = MappingReadPolicy::RequireReadable;
        return result;
    }
    fd_ = descriptor;
    return {};
#else
    return {Error::UnsupportedPlatform};
#endif
}
ReadResult ProcessReader::RefreshMappings()
{
#if defined(__linux__)
    if (pid_ <= 0)
        return {Error::InvalidArgument};
    const std::string path = "/proc/" + std::to_string(pid_) + "/maps";
    FILE *file = fopen(path.c_str(), "re");
    if (!file)
        return {SystemError(errno), 0, errno};
    std::vector<Mapping> next;
    char *line = nullptr;
    std::size_t capacity = 0;
    while (getline(&line, &capacity, file) >= 0)
    {
        unsigned long long begin = 0, end = 0;
        char permissions[5]{};
        if (sscanf(line, "%llx-%llx %4s", &begin, &end, permissions) != 3 || begin >= end ||
            end > std::numeric_limits<std::uintptr_t>::max())
        {
            free(line);
            fclose(file);
            return {Error::InvalidArgument};
        }
        next.push_back({static_cast<std::uintptr_t>(begin), static_cast<std::uintptr_t>(end),
                        permissions[0] == 'r'});
    }
    const int error = ferror(file) ? errno : 0;
    free(line);
    fclose(file);
    if (error)
        return {SystemError(error), 0, error};
    mappings_ = std::move(next);
    return {};
#else
    return {Error::UnsupportedPlatform};
#endif
}
ReadResult ProcessReader::Read(std::uintptr_t address, std::span<std::byte> destination) const
{
    if (Overflows(address, destination.size()))
        return {Error::AddressOverflow};
    if (destination.empty())
        return {};
#if defined(__linux__)
    if (fd_ < 0)
        return {Error::InvalidArgument};
    auto mapping = std::find_if(mappings_.begin(), mappings_.end(), [address](const auto &item)
                                { return item.begin <= address && address < item.end; });
    auto cursor = address;
    const auto end = address + destination.size();
    while (cursor < end)
    {
        if (mapping == mappings_.end() || mapping->begin > cursor || mapping->end <= cursor)
            return {Error::Unmapped};
        if (!mapping->readable && policy_ == MappingReadPolicy::RequireReadable)
            return {Error::PermissionDenied};
        cursor = std::min(end, mapping->end);
        ++mapping;
    }
    if (address > static_cast<std::uintptr_t>(std::numeric_limits<off_t>::max()) ||
        destination.size() > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
        return {Error::AddressOverflow};
    ssize_t count;
    do
    {
        count = pread(fd_, destination.data(), destination.size(), static_cast<off_t>(address));
    } while (count < 0 && errno == EINTR);
    if (count < 0)
    {
        const int error = errno;
        return {error == EIO || error == EFAULT ? Error::Unmapped : SystemError(error), 0, error};
    }
    const auto size = static_cast<std::size_t>(count);
    return {size == destination.size() ? Error::None : Error::ShortRead, size};
#else
    return {Error::UnsupportedPlatform};
#endif
}
Error ModuleCatalog::Replace(std::vector<ModuleIdentity> modules)
{
    for (const auto &module : modules)
        if (module.path.empty() || module.size == 0 || Overflows(module.base, module.size))
            return Error::InvalidArgument;
    if (modules != modules_)
    {
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            return Error::BudgetExceeded;
        modules_ = std::move(modules);
        ++generation_;
    }
    return Error::None;
}
} // namespace AndSwapChainHook::Memory
