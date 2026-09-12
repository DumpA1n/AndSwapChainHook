#include <AndSwapChainHook/HookTransaction.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dobby.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #value);                                    \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
namespace
{
void *selectedPage = nullptr;
int failWrite = 0, failRestore = 0;
__attribute__((noinline)) int Replacement()
{
    return 19;
}
using Function = int (*)();
int Permissions(void *address)
{
    FILE *file = fopen("/proc/self/maps", "re");
    CHECK(file);
    char line[2048], mode[5];
    unsigned long begin, end;
    int result = -1;
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "%lx-%lx %4s", &begin, &end, mode) == 3 &&
            reinterpret_cast<uintptr_t>(address) >= begin &&
            reinterpret_cast<uintptr_t>(address) < end)
        {
            result = (mode[0] == 'r' ? PROT_READ : 0) | (mode[1] == 'w' ? PROT_WRITE : 0) |
                     (mode[2] == 'x' ? PROT_EXEC : 0);
            break;
        }
    fclose(file);
    return result;
}
} // namespace
extern "C" int __real_mprotect(void *, size_t, int);
extern "C" int __wrap_mprotect(void *address, size_t size, int protection)
{
    int &remaining = (protection & PROT_WRITE) ? failWrite : failRestore;
    if (address == selectedPage && remaining > 0)
    {
        --remaining;
        errno = EACCES;
        return -1;
    }
    return __real_mprotect(address, size, protection);
}
int main()
{
    using namespace AndSwapChainHook::Instrumentation;
    const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void *pages =
        mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(pages != MAP_FAILED);
    auto *address = static_cast<uint8_t *>(pages) + pageSize - 8;
    const uint32_t body[] = {0x528000e0, 0xd65f03c0, 0xd503201f, 0xd503201f};
    memcpy(address, body, sizeof(body));
    CHECK(mprotect(pages, pageSize, PROT_READ | PROT_EXEC) == 0);
    CHECK(mprotect(static_cast<uint8_t *>(pages) + pageSize, pageSize,
                   PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
    __builtin___clear_cache(reinterpret_cast<char *>(address),
                            reinterpret_cast<char *>(address) + sizeof(body));
    selectedPage = pages;
    auto call = reinterpret_cast<Function>(address);
    CHECK(call() == 7);
    HookBackend backend{nullptr, [](void *, void *target, void *replacement, void **original)
                        { return DobbyHook(target, replacement, original); },
                        [](void *, void *target) { return DobbyDestroy(target); }};
    HookTransaction transaction(backend);
    void *original = nullptr;
    const HookRequest request{address, reinterpret_cast<void *>(Replacement), &original};
    failWrite = 1;
    auto result = transaction.Install(std::span(&request, 1), true);
    CHECK(result.error == HookError::InstallationFailed && result.backendError == -EACCES);
    CHECK(result.retainedHooks == 0 && call() == 7 &&
          Permissions(pages) == (PROT_READ | PROT_EXEC));
    failRestore = 2;
    result = transaction.Install(std::span(&request, 1), true);
    CHECK(result.error == HookError::RollbackFailed && result.backendError == -EACCES);
    CHECK(result.retainedHooks == 1 && original && transaction.Size() == 1);
    CHECK(transaction.Restore(true) && transaction.Size() == 0 && call() == 7);
    CHECK(Permissions(pages) == (PROT_READ | PROT_EXEC));
    CHECK(Permissions(static_cast<uint8_t *>(pages) + pageSize) ==
          (PROT_READ | PROT_WRITE | PROT_EXEC));
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        CHECK(transaction.Install(std::span(&request, 1), true));
        CHECK(call() == 19 && reinterpret_cast<Function>(original)() == 7);
        failWrite = 1;
        result = transaction.Restore(true);
        CHECK(result.error == HookError::RestorationFailed && result.retainedHooks == 1);
        CHECK(call() == 19);
        CHECK(transaction.Restore(true) && call() == 7);
    }
    CHECK(transaction.Install(std::span(&request, 1), true));
    uint8_t installed[sizeof(body)];
    memcpy(installed, address, sizeof(body));
    CHECK(mprotect(pages, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
    CHECK(transaction.Restore(true).backendError == -EBUSY && transaction.Size() == 1);
    address[0] ^= 1;
    CHECK(mprotect(pages, pageSize, PROT_READ | PROT_EXEC) == 0);
    CHECK(transaction.Restore(true).backendError == -EBUSY && transaction.Size() == 1);
    CHECK(mprotect(pages, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
    memcpy(address, installed, sizeof(body));
    CHECK(mprotect(pages, pageSize, PROT_READ | PROT_EXEC) == 0);
    CHECK(transaction.Restore(true) && call() == 7);
    CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_WRITE) == 0);
    reinterpret_cast<uint32_t *>(address)[0] = 0xd503233f;
    CHECK(DobbyHook(address, reinterpret_cast<void *>(Replacement), &original) == -ENOTSUP &&
          !original);
    const uint32_t rejected[] = {0x98000000, 0xd8000000, 0x1c000000, 0x5c000000,
                                 0x14000002, 0x94000002, 0x54000040, 0x34000040,
                                 0x36000040, 0x5400008e, 0x5400008f, 0x10000040};
    for (const uint32_t instruction : rejected)
    {
        CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_WRITE) == 0);
        memcpy(address, body, sizeof(body));
        reinterpret_cast<uint32_t *>(address)[0] = instruction;
        CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_EXEC) == 0);
        CHECK(DobbyHook(address, reinterpret_cast<void *>(Replacement), &original) == -ENOTSUP);
        CHECK(!original && reinterpret_cast<uint32_t *>(address)[0] == instruction);
        CHECK(Permissions(pages) == (PROT_READ | PROT_EXEC));
    }
    CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_WRITE) == 0);
    const uint32_t externalBranch[] = {0x14000004, 0xd503201f, 0xd503201f,
                                       0xd503201f, 0x528000e0, 0xd65f03c0};
    memcpy(address, externalBranch, sizeof(externalBranch));
    CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_EXEC) == 0);
    __builtin___clear_cache(reinterpret_cast<char *>(address),
                            reinterpret_cast<char *>(address) + sizeof(externalBranch));
    CHECK(DobbyHook(address, reinterpret_cast<void *>(Replacement), &original) == 0);
    CHECK(call() == 19 && reinterpret_cast<Function>(original)() == 7);
    CHECK(DobbyDestroy(address) == 0 && call() == 7);
    CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_WRITE) == 0);
    memcpy(address, body, sizeof(body));
    CHECK(mprotect(pages, pageSize * 2, PROT_READ | PROT_EXEC) == 0);
    bool exhausted = false;
    for (int attempt = 0; attempt < 256; ++attempt)
    {
        const int installedResult =
            DobbyHook(address, reinterpret_cast<void *>(Replacement), &original);
        if (installedResult == -ENOSPC)
        {
            CHECK(!original && call() == 7);
            exhausted = true;
            break;
        }
        CHECK(installedResult == 0 && original);
        CHECK(DobbyDestroy(address) == 0);
    }
    CHECK(exhausted);
    selectedPage = nullptr;
    CHECK(munmap(pages, pageSize * 2) == 0);
    std::printf(
        "R01/R22: production Dobby hook, partial installation, permission failure, retained "
        "restoration, foreign ownership, two-page exact permissions, PAC rejection passed; %s\n",
        DobbyBuildRevision());
}
