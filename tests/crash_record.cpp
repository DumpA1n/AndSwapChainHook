#include <AndSwapChainHook/CrashRecorder.h>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value "\n";                                             \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

namespace
{
void PreviousHandler(int)
{
    _exit(91);
}
void DifferentOwner(int) {}
} // namespace

int main()
{
    int descriptors[2];
    CHECK(pipe(descriptors) == 0);
    const auto child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        struct sigaction previous{};
        previous.sa_handler = PreviousHandler;
        sigemptyset(&previous.sa_mask);
        CHECK(sigaction(SIGABRT, &previous, nullptr) == 0);
        CHECK(AndSwapChainHook::CrashRecorder::Install(descriptors[1]));
        close(descriptors[1]);
        raise(SIGABRT);
        _exit(92);
    }
    close(descriptors[1]);
    AndSwapChainHook::CrashRecorder::Record record;
    CHECK(read(descriptors[0], &record, sizeof(record)) == sizeof(record));
    CHECK(record.magic == 0x43525348 && record.version == 1 && record.signal == SIGABRT);
    close(descriptors[0]);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 91);

    CHECK(AndSwapChainHook::CrashRecorder::Install(-1).error ==
          AndSwapChainHook::CrashRecorder::Error::InvalidDescriptor);
    int sink = open("/dev/null", O_WRONLY);
    CHECK(sink >= 0);
    struct sigaction original{}, replacement{};
    CHECK(sigaction(SIGBUS, nullptr, &original) == 0);
    CHECK(AndSwapChainHook::CrashRecorder::Install(sink));
    CHECK(AndSwapChainHook::CrashRecorder::Install(sink).error ==
          AndSwapChainHook::CrashRecorder::Error::AlreadyInstalled);
    CHECK(AndSwapChainHook::CrashRecorder::Uninstall().error ==
          AndSwapChainHook::CrashRecorder::Error::QuiescenceRequired);
    replacement.sa_handler = DifferentOwner;
    sigemptyset(&replacement.sa_mask);
    CHECK(sigaction(SIGBUS, &replacement, nullptr) == 0);
    CHECK(AndSwapChainHook::CrashRecorder::Uninstall(true).error ==
          AndSwapChainHook::CrashRecorder::Error::OwnershipConflict);
    struct sigaction observed{};
    CHECK(sigaction(SIGBUS, nullptr, &observed) == 0 && observed.sa_handler == DifferentOwner);
    CHECK(sigaction(SIGBUS, &original, nullptr) == 0);
    close(sink);
}
