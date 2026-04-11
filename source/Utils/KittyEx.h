#pragma once

#include <sys/cdefs.h>
#include <thread>
#if defined(kUSE_KITTYMEMORYEX)
#include "KittyMemoryEx/KittyMemoryEx.hpp"
#include "KittyMemoryEx/KittyMemoryMgr.hpp"
#include "KittyMemoryEx/KittyScanner.hpp"
#else
#include "KittyMemory/KittyInclude.hpp"
#endif
#include "Logger.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>

namespace KT
{

inline uintptr_t Strip(uintptr_t address) {
    return address & (uintptr_t)0x7fffffffff;
}

inline bool IsValid(uintptr_t address) {
    if (!address || address < 0x10000000)
        return false;
    uintptr_t highBits = address & ~(uintptr_t)0x7fffffffff;
    return highBits == 0 || highBits == (uintptr_t)0xB400000000000000;
}

template<class T>
inline bool IsValid(const T& c) {
    return IsValid((uintptr_t)c);
}

enum class MemError : uint8_t {
    None = 0,
    InvalidAddress,
    ReadFailed,
    WriteFailed,
    SyscallFailed,
    IOWriteFailed,
    OpenFailed,
    NotInitialized,
};

inline thread_local MemError t_lastError = MemError::None;

inline void ClearError() { t_lastError = MemError::None; }
inline void SetError(MemError e) { t_lastError = e; }
inline MemError LastError() { auto e = t_lastError; t_lastError = MemError::None; return e; }
inline bool HasError() { return t_lastError != MemError::None; }

inline const char* ErrorString(MemError e) {
    switch (e) {
        case MemError::None:           return "None";
        case MemError::InvalidAddress: return "InvalidAddress";
        case MemError::ReadFailed:     return "ReadFailed";
        case MemError::WriteFailed:    return "WriteFailed";
        case MemError::SyscallFailed:  return "SyscallFailed";
        case MemError::IOWriteFailed:  return "IOWriteFailed";
        case MemError::OpenFailed:     return "OpenFailed";
        case MemError::NotInitialized: return "NotInitialized";
    }
    return "Unknown";
}

// ============================================================
//  Global state
//
//  kUSE_KITTYMEMORYEX → KittyMemoryEx 后端
//  默认               → KittyMemory 后端
// ============================================================
namespace detail {
    inline pid_t g_pid = 0;
#if defined(kUSE_KITTYMEMORYEX)
    inline KittyMemoryMgr g_mgr;       // RW + FastRW + ForceRW (pvm syscall)
#endif
} // namespace detail

// ============================================================
//  Init / Getpid
// ============================================================
inline bool Init(const std::string& processName = "") {
    ClearError();
#if defined(kUSE_KITTYMEMORYEX)
    detail::g_pid = processName.empty() ? getpid() : KittyMemoryEx::getProcessID(processName);
    if (detail::g_pid < 1) {
        SetError(MemError::NotInitialized);
        LOGE("KT::Init error: failed to get pid: %d", detail::g_pid);
        return false;
    }
    if (!detail::g_mgr.initialize(detail::g_pid, EK_MEM_OP_SYSCALL, false)) {
        SetError(MemError::NotInitialized);
        LOGE("KT::Init error: failed to init KittyMemoryMgr");
        return false;
    }
    LOGI("KT::Init success, pid: %d", detail::g_pid);
    return true;
#else
    detail::g_pid = getpid();
    return true;
#endif
}

inline pid_t GetPid() { return detail::g_pid; }

// ============================================================
//  ElfScanner
// ============================================================
inline void ElfScan(const std::string& elfName, ElfScanner& scanner) {
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#if defined(kUSE_KITTYMEMORYEX)
        scanner = detail::g_mgr.elfScanner.findElf(elfName);
#else
        scanner = ElfScanner::findElf(elfName);
#endif
    } while (!scanner.isValid());
}

// ============================================================
//  FastRW: 最快路径
//  KittyMemory: memcpy           KittyMemoryEx: g_mgr pvm
// ============================================================
inline bool FastRead(uint64_t address, void* buffer, size_t len) {
#if defined(kUSE_KITTYMEMORYEX)
    size_t ret = detail::g_mgr.readMem(address, buffer, len);
    if (ret != len) { SetError(MemError::ReadFailed); return false; }
    return true;
#else
    std::memcpy(buffer, reinterpret_cast<const void*>(address), len);
    return true;
#endif
}

inline bool FastWrite(uint64_t address, const void* data, size_t len) {
#if defined(kUSE_KITTYMEMORYEX)
    size_t ret = detail::g_mgr.writeMem(address, const_cast<void*>(data), len);
    if (ret != len) { SetError(MemError::WriteFailed); return false; }
    return true;
#else
    std::memcpy(reinterpret_cast<void*>(address), data, len);
    return true;
#endif
}

// ============================================================
//  RW: 默认读写
//  KittyMemory: pvm syscall      KittyMemoryEx: g_mgr pvm
// ============================================================
inline bool Read(uint64_t address, void* buffer, size_t len) {
#if defined(kUSE_KITTYMEMORYEX)
    size_t ret = detail::g_mgr.readMem(address, buffer, len);
    if (ret != len) { SetError(MemError::ReadFailed); return false; }
    return true;
#else
    size_t ret = KittyMemory::syscallMemRead(static_cast<uintptr_t>(address), buffer, len);
    if (ret != len) { SetError(MemError::ReadFailed); return false; }
    return true;
#endif
}

inline bool Write(uint64_t address, const void* data, size_t len) {
#if defined(kUSE_KITTYMEMORYEX)
    size_t ret = detail::g_mgr.writeMem(address, const_cast<void*>(data), len);
    if (ret != len) { SetError(MemError::WriteFailed); return false; }
    return true;
#else
    size_t ret = KittyMemory::syscallMemWrite(static_cast<uintptr_t>(address),
                                              const_cast<void*>(data), len);
    if (ret != len) { SetError(MemError::WriteFailed); return false; }
    return true;
#endif
}

// ============================================================
//  MemProtect: 页对齐 mprotect 封装
// ============================================================
inline int MemProtect(uintptr_t address, size_t len, int protection) {
    uintptr_t pageAddr = address & ~(uintptr_t)(sysconf(_SC_PAGESIZE) - 1);
    size_t protLen = address + len - pageAddr;
    return mprotect(reinterpret_cast<void*>(pageAddr), protLen, protection);
}

// ============================================================
//  ForceRW: 强制读写（必要时 mprotect 后再 rw）
// ============================================================
inline bool ForceRead(uint64_t address, void* buffer, size_t len) {
#if defined(kUSE_KITTYMEMORYEX)
    auto map = KittyMemoryEx::getAddressMap(detail::g_pid, static_cast<uintptr_t>(address));
    int origProt = -1;
    if (map.isValid() && !map.readable) {
        origProt = map.protection;
        if (MemProtect(static_cast<uintptr_t>(address), len, origProt | PROT_READ) != 0) {
            SetError(MemError::SyscallFailed); return false;
        }
    }
    size_t ret = detail::g_mgr.readMem(address, buffer, len);
    if (origProt >= 0) MemProtect(static_cast<uintptr_t>(address), len, origProt);
    if (ret != len) { SetError(MemError::ReadFailed); return false; }
    return true;
#else
    if (!KittyMemory::memRead(reinterpret_cast<const void*>(address), buffer, len)) {
        SetError(MemError::ReadFailed);
        return false;
    }
    return true;
#endif
}

inline bool ForceWrite(uint64_t address, const void* data, size_t len) {
#if defined(kUSE_KITTYMEMORYEX)
    auto map = KittyMemoryEx::getAddressMap(detail::g_pid, static_cast<uintptr_t>(address));
    int origProt = -1;
    if (map.isValid() && !map.writeable) {
        origProt = map.protection;
        if (MemProtect(static_cast<uintptr_t>(address), len, origProt | PROT_WRITE) != 0) {
            SetError(MemError::SyscallFailed); return false;
        }
    }
    size_t ret = detail::g_mgr.writeMem(address, const_cast<void*>(data), len);
    if (origProt >= 0) MemProtect(static_cast<uintptr_t>(address), len, origProt);
    if (ret != len) { SetError(MemError::WriteFailed); return false; }
    return true;
#else
    if (!KittyMemory::memWrite(reinterpret_cast<void*>(address), data, len)) {
        SetError(MemError::WriteFailed);
        return false;
    }
    return true;
#endif
}

// ============================================================
//  Template helpers — pointer chain read (共用实现)
// ============================================================
namespace detail {
using RawReadFn = bool(*)(uint64_t, void*, size_t);

template<RawReadFn ReadFn, typename Ret, typename T, typename... Offsets>
inline Ret ChainRead(T base, Offsets... offsets) {
    uint64_t address = (uint64_t)base;
    if constexpr (sizeof...(offsets) == 0) {
        Ret value{};
        ReadFn(address, &value, sizeof(Ret));
        return value;
    } else {
        std::array<uint64_t, sizeof...(offsets)> offset_array = {static_cast<uint64_t>(offsets)...};
        for (size_t i = 0; i < sizeof...(offsets) - 1; ++i) {
            uint64_t next = 0;
            if (!ReadFn(address + offset_array[i], &next, sizeof(next)) || !IsValid(next))
                return Ret{};
            address = next;
        }
        Ret value{};
        ReadFn(address + offset_array[sizeof...(offsets) - 1], &value, sizeof(Ret));
        return value;
    }
}
} // namespace detail

// ============================================================
//  Template overloads — FastRead / Read / ForceRead
// ============================================================
template<typename Ret, typename T, typename... Offsets>
inline Ret FastRead(T base, Offsets... offsets) {
    return detail::ChainRead<FastRead, Ret>(base, offsets...);
}

template<typename Ret, typename T, typename... Offsets>
inline Ret Read(T base, Offsets... offsets) {
    return detail::ChainRead<Read, Ret>(base, offsets...);
}

template<typename Ret, typename T, typename... Offsets>
inline Ret ForceRead(T base, Offsets... offsets) {
    return detail::ChainRead<ForceRead, Ret>(base, offsets...);
}

// ============================================================
//  Template overloads — FastWrite / Write / ForceWrite
// ============================================================
template<typename T>
inline typename std::enable_if<
    !std::is_convertible<T, std::string>::value &&
    !std::is_convertible<T, std::vector<uint8_t>>::value,
    bool>::type
FastWrite(uintptr_t address, T value) {
    return FastWrite(address, &value, sizeof(T));
}

template<typename T>
inline typename std::enable_if<
    !std::is_convertible<T, std::string>::value &&
    !std::is_convertible<T, std::vector<uint8_t>>::value,
    bool>::type
Write(uintptr_t address, T value) {
    return Write(address, &value, sizeof(T));
}

template<typename T>
inline typename std::enable_if<
    !std::is_convertible<T, std::string>::value &&
    !std::is_convertible<T, std::vector<uint8_t>>::value,
    bool>::type
ForceWrite(uintptr_t address, T value) {
    return ForceWrite(address, &value, sizeof(T));
}

// ============================================================
//  Convenience — hex string / byte array write
// ============================================================
inline bool ConvStringToByteArray(const std::string& hexString, std::vector<uint8_t>& byteArray) {
    if (hexString.size() < 2 || hexString.size() % 2 != 0) return false;
    for (size_t i = 0; i < hexString.size(); i += 2) {
        int byte = std::stoi(hexString.substr(i, 2), nullptr, 16);
        byteArray.push_back(static_cast<uint8_t>(byte));
    }
    return true;
}

inline bool Write(uintptr_t address, const std::string& hexString) {
    std::vector<uint8_t> byteArray;
    ConvStringToByteArray(hexString, byteArray);
    return Write(address, byteArray.data(), byteArray.size());
}

inline bool Write(uintptr_t address, const std::vector<uint8_t>& byteArray) {
    return Write(address, (void*)byteArray.data(), byteArray.size());
}

}
