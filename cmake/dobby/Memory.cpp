#include "Memory.h"
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>

namespace
{
constexpr std::size_t MaxPages = 17;
constexpr std::size_t MaxBytes = 65536;
struct Page
{
    uintptr_t address = 0;
    unsigned long inode = 0, offset = 0;
    unsigned int major = 0, minor = 0;
    int original = 0, expected = 0;
    char sharing = 0;
};
struct Record
{
    void *address = nullptr;
    std::size_t size = 0, count = 0;
    bool owned = false;
    std::array<Page, MaxPages> pages{};
};
std::array<Record, 256> records;
std::mutex mutex;

bool ReadPage(uintptr_t address, Page &page)
{
    FILE *file = fopen("/proc/self/maps", "re");
    if (!file)
        return false;
    char line[2048];
    bool found = false;
    while (fgets(line, sizeof(line), file))
    {
        unsigned long begin, end, offset, inode;
        unsigned int major, minor;
        char mode[5]{};
        if (sscanf(line, "%lx-%lx %4s %lx %x:%x %lu", &begin, &end, mode, &offset, &major, &minor,
                   &inode) != 7 ||
            address < begin || address >= end)
            continue;
        page.address = address;
        page.inode = inode;
        page.offset = inode ? offset + address - begin : 0;
        page.major = major;
        page.minor = minor;
        page.sharing = mode[3];
        page.original = (mode[0] == 'r' ? PROT_READ : 0) | (mode[1] == 'w' ? PROT_WRITE : 0) |
                        (mode[2] == 'x' ? PROT_EXEC : 0);
        page.expected = page.original;
        found = true;
        break;
    }
    fclose(file);
    return found;
}
bool Same(const Page &expected, const Page &actual)
{
    return expected.inode == actual.inode && expected.offset == actual.offset &&
           expected.major == actual.major && expected.minor == actual.minor &&
           expected.sharing == actual.sharing && expected.expected == actual.original;
}
int Check(Record &record)
{
    for (std::size_t i = 0; i < record.count; ++i)
    {
        Page actual;
        if (!ReadPage(record.pages[i].address, actual))
            return -EFAULT;
        if (!Same(record.pages[i], actual))
            return -EBUSY;
    }
    return 0;
}
int Snapshot(void *address, std::size_t size, Record &record)
{
    const auto begin = reinterpret_cast<uintptr_t>(address);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (!address || !size || size > MaxBytes || pageSize < 4096 || (pageSize & (pageSize - 1)) ||
        size - 1 > std::numeric_limits<uintptr_t>::max() - begin)
        return -EINVAL;
    const auto first = begin & ~static_cast<uintptr_t>(pageSize - 1);
    const auto last = (begin + size - 1) & ~static_cast<uintptr_t>(pageSize - 1);
    record.count = (last - first) / pageSize + 1;
    if (record.count > MaxPages)
        return -E2BIG;
    record.address = address;
    record.size = size;
    for (std::size_t i = 0; i < record.count; ++i)
    {
        auto &page = record.pages[i];
        if (!ReadPage(first + i * pageSize, page) || !(page.original & PROT_READ))
            return -EACCES;
        for (const auto &other : records)
            if (other.address)
                for (std::size_t j = 0; j < other.count; ++j)
                    if (other.pages[j].address == page.address &&
                        (other.pages[j].expected != other.pages[j].original ||
                         !Same(other.pages[j], page)))
                        return -EBUSY;
    }
    return 0;
}
Record *Find(void *address)
{
    for (auto &record : records)
        if (record.address == address)
            return &record;
    return nullptr;
}
int Prepare(void *address, std::size_t size, Record *&result)
{
    result = Find(address);
    if (result)
        return result->size == size ? Check(*result) : -EBUSY;
    Record snapshot;
    const int error = Snapshot(address, size, snapshot);
    if (error)
        return error;
    for (auto &record : records)
        if (!record.address)
        {
            record = snapshot;
            result = &record;
            return 0;
        }
    return -ENOSPC;
}
int RestorePermissions(Record &record)
{
    int error = 0;
    const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    for (std::size_t i = record.count; i > 0; --i)
    {
        auto &page = record.pages[i - 1];
        if (page.expected == page.original)
            continue;
        if (mprotect(reinterpret_cast<void *>(page.address), pageSize, page.original))
        {
            if (!error)
                error = -errno;
        }
        else
            page.expected = page.original;
    }
    return error;
}
} // namespace
bool DobbyReadable(void *address, std::size_t size)
{
    std::lock_guard lock(mutex);
    // Readability does not confer ownership; callers own the mapping lifetime.
    const auto begin = reinterpret_cast<uintptr_t>(address);
    if (!address || !size || size > MaxBytes || size - 1 > UINTPTR_MAX - begin)
        return false;
    const auto pageSize = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    for (auto page = begin & ~(pageSize - 1);; page += pageSize)
    {
        Page actual;
        if (!ReadPage(page, actual) || !(actual.original & PROT_READ))
            return false;
        if ((begin + size - 1) - page < pageSize)
            return true;
    }
}
int DobbyBeginPatch(void *address, std::size_t size)
{
    std::lock_guard lock(mutex);
    if (Find(address))
        return -EBUSY;
    Record *record = nullptr;
    const int error = Prepare(address, size, record);
    if (!error)
    {
        for (std::size_t i = 0; i < record->count; ++i)
            if (!(record->pages[i].original & PROT_EXEC))
            {
                *record = {};
                return -EACCES;
            }
        record->owned = true;
    }
    return error;
}
int DobbyEndPatch(void *address)
{
    std::lock_guard lock(mutex);
    auto *record = Find(address);
    if (!record || !record->owned)
        return -ENOENT;
    const int checked = Check(*record);
    if (checked)
        return checked;
    const int error = RestorePermissions(*record);
    if (!error)
        *record = {};
    return error;
}
extern "C" int DobbyCodePatch(void *address, uint8_t *buffer, uint32_t size)
{
    if (!buffer)
        return -EINVAL;
    std::lock_guard lock(mutex);
    Record *record = nullptr;
    const int prepared = Prepare(address, size, record);
    if (prepared)
        return prepared;
    int error = 0;
    const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    for (std::size_t i = 0; i < record->count; ++i)
    {
        auto &page = record->pages[i];
        const int desired = page.original | PROT_WRITE;
        if (page.expected == desired)
            continue;
        if (mprotect(reinterpret_cast<void *>(page.address), pageSize, desired))
        {
            error = -errno;
            break;
        }
        page.expected = desired;
    }
    if (!error)
    {
        memcpy(address, buffer, size);
        __builtin___clear_cache(static_cast<char *>(address), static_cast<char *>(address) + size);
    }
    const int restored = RestorePermissions(*record);
    if (!restored && !record->owned)
        *record = {};
    return restored ? restored : error;
}
