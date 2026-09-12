#include <AndSwapChainHook/CrashRecorder.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <mutex>
#if defined(__ANDROID__) && defined(__aarch64__)
#include <ucontext.h>
#endif
#include <unistd.h>

namespace AndSwapChainHook::CrashRecorder
{
namespace
{
constexpr std::array<int, 6> signals{SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP};
struct State
{
    int descriptor = -1;
    std::array<struct sigaction, signals.size()> previous{};
};
State state;
std::atomic<const State *> published{nullptr};
std::atomic<std::uint64_t> dropped{0};
static_assert(decltype(published)::is_always_lock_free);
static_assert(decltype(dropped)::is_always_lock_free);
std::mutex ownership;
std::array<bool, signals.size()> installed{};
bool used = false;

void OnSignal(int number, siginfo_t *info, void *context)
{
    const int savedErrno = errno;
    const State *current = published.load(std::memory_order_acquire);
    if (!current)
        _exit(128 + number);
    Record record;
    record.signal = number;
    if (info)
    {
        record.code = info->si_code;
        if (info->si_code > 0 &&
            (number == SIGSEGV || number == SIGBUS || number == SIGILL || number == SIGFPE))
            record.address = reinterpret_cast<std::uintptr_t>(info->si_addr);
    }
#if defined(__ANDROID__) && defined(__aarch64__)
    if (context)
    {
        const auto &registers = static_cast<ucontext_t *>(context)->uc_mcontext;
        record.pc = registers.pc;
        record.sp = registers.sp;
        record.lr = registers.regs[30];
    }
#else
    (void)context;
#endif
    const ssize_t count = ::write(current->descriptor, &record, sizeof(record));
    if (count != sizeof(record))
        dropped.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t i = 0; i < signals.size(); ++i)
    {
        if (signals[i] == number)
        {
            // Redelivery follows the saved disposition. The binary record retains
            // original fault metadata; the redelivered signal has a new siginfo.
            ::sigaction(number, &current->previous[i], nullptr);
            errno = savedErrno;
            ::raise(number);
            return;
        }
    }
    _exit(128 + number);
}
} // namespace

Result Install(int descriptor, bool useCallerAltStack)
{
    std::lock_guard lock(ownership);
    if (used)
    {
        for (bool value : installed)
            if (value)
                return {Error::AlreadyInstalled};
        return {Error::Retired};
    }
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) == O_RDONLY)
        return {Error::InvalidDescriptor, flags < 0 ? errno : 0};
    const int sink = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (sink < 0)
        return {Error::SystemError, errno};
    for (std::size_t i = 0; i < signals.size(); ++i)
    {
        if (::sigaction(signals[i], nullptr, &state.previous[i]) != 0)
        {
            const int failure = errno;
            ::close(sink);
            return {Error::SystemError, failure};
        }
    }
    state.descriptor = sink;
    used = true;
    published.store(&state, std::memory_order_release);
    struct sigaction action{};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | (useCallerAltStack ? SA_ONSTACK : 0);
    sigfillset(&action.sa_mask);
    for (std::size_t i = 0; i < signals.size(); ++i)
    {
        if (::sigaction(signals[i], &action, nullptr) != 0)
        {
            const int failure = errno;
            for (std::size_t j = 0; j < i; ++j)
                if (::sigaction(signals[j], &state.previous[j], nullptr) == 0)
                    installed[j] = false;
            return {Error::SystemError, failure};
        }
        installed[i] = true;
    }
    return {};
}

Result Uninstall(bool signalsQuiescent)
{
    if (!signalsQuiescent)
        return {Error::QuiescenceRequired};
    std::lock_guard lock(ownership);
    Result result;
    for (std::size_t i = 0; i < signals.size(); ++i)
    {
        if (!installed[i])
            continue;
        struct sigaction action{};
        if (::sigaction(signals[i], nullptr, &action) != 0)
        {
            result = {Error::SystemError, errno};
            continue;
        }
        if (!(action.sa_flags & SA_SIGINFO) || action.sa_sigaction != OnSignal)
        {
            result = {Error::OwnershipConflict};
            continue;
        }
        if (::sigaction(signals[i], &state.previous[i], nullptr) != 0)
        {
            result = {Error::SystemError, errno};
            continue;
        }
        installed[i] = false;
    }
    return result;
}
std::uint64_t DroppedRecords()
{
    return dropped.load(std::memory_order_relaxed);
}
} // namespace AndSwapChainHook::CrashRecorder
