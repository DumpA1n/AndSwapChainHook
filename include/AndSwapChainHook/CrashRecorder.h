#pragma once
#include <cstdint>

namespace AndSwapChainHook::CrashRecorder
{
enum class Error
{
    None,
    InvalidDescriptor,
    AlreadyInstalled,
    Retired,
    SystemError,
    OwnershipConflict,
    QuiescenceRequired
};
struct Result
{
    Error error = Error::None;
    int systemError = 0;
    bool canUnload = false;
    explicit operator bool() const
    {
        return error == Error::None;
    }
};
// Fixed-size native-endian record. Symbolication belongs to the offline reader.
struct Record
{
    std::uint32_t magic = 0x43525348;
    std::uint32_t version = 1;
    std::int32_t signal = 0;
    std::int32_t code = 0;
    std::uint64_t address = 0;
    std::uint64_t pc = 0;
    std::uint64_t sp = 0;
    std::uint64_t lr = 0;
};

// One installation is permitted per process. The sink is duplicated and retained
// for process lifetime, including after Uninstall, to protect late signal entry.
// The host owns signal dispositions and any per-thread alternate signal stacks.
// A nonblocking pipe or preopened append-only file is the intended sink.
Result Install(int descriptor, bool useCallerAltStack = false);
// Signals and cached handler entry addresses must be retired by the host.
Result Uninstall(bool signalsQuiescent = false);
std::uint64_t DroppedRecords();
} // namespace AndSwapChainHook::CrashRecorder
