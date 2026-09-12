#include "InterceptRouting/InlineHookRouting.h"
#include "Memory.h"
#include <cerrno>
#include <cstring>
#include <mutex>
#include <new>

namespace
{
std::mutex operations;
std::size_t attempts = 0;
constexpr std::size_t Capacity = 256;
bool UnsupportedWindow(void *address, std::size_t size)
{
    const auto begin = reinterpret_cast<uintptr_t>(address);
    const auto *words = static_cast<const uint32_t *>(address);
    for (std::size_t i = 0; i < size / 4; ++i)
    {
        const uint32_t word = words[i];
        if ((word & 0x3b000000u) == 0x18000000u)
            return true;
        if ((word & 0xff000010u) == 0x54000000u && (word & 15u) >= 14u)
            return true;
        if ((word & 0x9f000000u) == 0x10000000u)
        {
            const uint32_t immediate = ((word >> 5) & 0x7ffffu) << 2 | ((word >> 29) & 3u);
            const int64_t sign = int64_t{1} << 20;
            const int64_t displacement = (static_cast<int64_t>(immediate) ^ sign) - sign;
            const uintptr_t destination = begin + i * 4 + static_cast<uintptr_t>(displacement);
            if (destination >= begin && destination - begin < size)
                return true;
        }
        uint32_t immediate = 0;
        unsigned bits = 0;
        if ((word & 0x7c000000u) == 0x14000000u)
        {
            immediate = word & 0x03ffffffu;
            bits = 26;
        }
        else if ((word & 0xff000010u) == 0x54000000u || (word & 0x7e000000u) == 0x34000000u)
        {
            immediate = (word >> 5) & 0x7ffffu;
            bits = 19;
        }
        else if ((word & 0x7e000000u) == 0x36000000u)
        {
            immediate = (word >> 5) & 0x3fffu;
            bits = 14;
        }
        if (bits)
        {
            const int64_t sign = int64_t{1} << (bits - 1);
            const int64_t displacement = ((static_cast<int64_t>(immediate) ^ sign) - sign) * 4;
            const uintptr_t destination = begin + i * 4 + static_cast<uintptr_t>(displacement);
            if (destination >= begin && destination - begin < size)
                return true;
        }
    }
    return false;
}

} // namespace
// Near branch allocation and instruction instrumentation are outside this facade.
Trampoline *GenerateNearTrampolineBuffer(addr_t, addr_t)
{
    return nullptr;
}
extern "C" const char *DobbyBuildRevision()
{
    return "5dfc8546954ce3b3198132ab13fddb89ee92cdd7+andswapchain-contract-1";
}
extern "C" int DobbyHook(void *address, void *replacement, void **original)
{
    if (!original)
        return -EINVAL;
    *original = nullptr;
    try
    {
        if (!address || !replacement || reinterpret_cast<uintptr_t>(address) % 4 ||
            reinterpret_cast<uintptr_t>(replacement) % 4)
            return -EINVAL;
        std::lock_guard lock(operations);
        if (gInterceptor.find(reinterpret_cast<addr_t>(address)))
            return -EEXIST;
        if (attempts == Capacity)
            return -ENOSPC;
        if (!DobbyReadable(address, 16))
            return -EACCES;
        const auto *instructions = static_cast<const uint32_t *>(address);
        for (std::size_t i = 0; i < 4; ++i)
            if ((instructions[i] & 0xfffffc1fu) == 0xd503241fu || instructions[i] == 0xd503233fu ||
                instructions[i] == 0xd503237fu)
                return -ENOTSUP;
        ++attempts;
        auto *entry = new Interceptor::Entry(reinterpret_cast<addr_t>(address));
        entry->fake_func_addr = reinterpret_cast<addr_t>(replacement);
        auto *routing = new InlineHookRouting(entry, entry->fake_func_addr);
        entry->routing = routing;
        routing->GenerateTrampoline();
        const auto patchSize = routing->trampoline_size();
        if (!patchSize || patchSize > 16 || patchSize % 4 || UnsupportedWindow(address, patchSize))
            return -ENOTSUP;
        routing->GenerateRelocatedCode();
        if (!routing->error)
            routing->BackupOriginCode();
        if (routing->error || !entry->relocated.addr() || !entry->origin_code_)
            return -EIO;
        gInterceptor.add(entry);
        const int permission = DobbyBeginPatch(address, entry->patched.size);
        if (permission)
        {
            gInterceptor.remove(reinterpret_cast<addr_t>(address));
            return permission;
        }
        // A non-null original on failure identifies an entry that still needs restoration.
        *original = reinterpret_cast<void *>(entry->relocated.addr());
        routing->Active();
        return routing->error;
    }
    catch (const std::bad_alloc &)
    {
        return -ENOMEM;
    }
}
extern "C" int DobbyDestroy(void *address)
{
    std::lock_guard lock(operations);
    auto *entry = gInterceptor.find(reinterpret_cast<addr_t>(address));
    if (!entry)
        return -ENOENT;
    if (!DobbyReadable(address, entry->patched.size))
        return -EACCES;
    auto *routing = entry->routing;
    if (memcmp(address, entry->origin_code_, entry->patched.size) != 0 &&
        memcmp(address, reinterpret_cast<void *>(routing->trampoline_addr()),
               entry->patched.size) != 0)
        return -EBUSY;
    const int error = DobbyCodePatch(address, entry->origin_code_, entry->patched.size);
    if (error)
        return error;
    const int ended = DobbyEndPatch(address);
    if (ended)
        return ended;
    gInterceptor.remove(reinterpret_cast<addr_t>(address));
    // Allocated entries and code remain resident, bounded by the attempt capacity.
    return 0;
}
