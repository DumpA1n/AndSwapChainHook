#pragma once

#include <sys/cdefs.h>
#include <thread>
#if defined(kBUILD_LIBRARY)
#include "KittyMemory/KittyInclude.hpp"
#else
#include <KittyMemoryEx/KittyMemoryEx.hpp>
#include "KittyMemoryEx/KittyMemoryMgr.hpp"
#include <KittyMemoryEx/KittyScanner.hpp>
#endif
#include "Logger.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h> // dladdr
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <asm/unistd.h>

namespace KT
{

inline uintptr_t Strip(uintptr_t address) {
    return address & (uintptr_t)0x7fffffffff;
}

inline bool IsValid(uintptr_t address) {
    // 高位只允许全 0 或 Android tagged pointer 前缀 0xB4000
    uintptr_t highBits = address & ~(uintptr_t)0x7fffffffff;
    if (highBits != 0 && highBits != (uintptr_t)0xB400000000000000)
        return false;

    uintptr_t stripped = Strip(address);
    if (!stripped || stripped < (uintptr_t)0x10000000)
        return false;

    return true;
}

template<class T>
inline bool IsValid(const T& c) {
    return IsValid((uintptr_t)c);
}

inline bool IsValidF(float value) {
    if (std::isnan(value)) {
        return false;
    }
    if (std::isinf(value)) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    return true;
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

inline void clearError() { t_lastError = MemError::None; }
inline void setError(MemError e) { t_lastError = e; }
inline MemError lastError() { auto e = t_lastError; t_lastError = MemError::None; return e; }
inline bool hasError() { return t_lastError != MemError::None; }

inline const char* errorString(MemError e) {
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
//  Platform backend — the ONLY place with #if defined
//
//  Safe  = KittyMemory (遍历 maps, mprotect)，安全但慢
//  Fast  = 直接 memcpy，零开销但调用者需自行保证地址可读写
// ============================================================
namespace detail {
#if defined(kBUILD_LIBRARY)
    struct MemBackend {
        bool init(const std::string&) { return true; }
        pid_t getpid() { return ::getpid(); }

        // ---------- Safe: KittyMemory (遍历 maps) ----------
        bool safeRead(uint64_t address, void* buffer, size_t len) {
            if (!KittyMemory::memRead(buffer, reinterpret_cast<void*>(address), len)) {
                setError(MemError::ReadFailed);
                return false;
            }
            return true;
        }

        bool safeWrite(uint64_t address, void* buffer, size_t len) {
            if (!KittyMemory::memWrite(reinterpret_cast<void*>(address), buffer, len)) {
                setError(MemError::WriteFailed);
                return false;
            }
            return true;
        }

        // ---------- Fast: 直接 memcpy ----------
        bool fastRead(uint64_t address, void* buffer, size_t len) {
            std::memcpy(buffer, reinterpret_cast<const void*>(address), len);
            return true;
        }

        bool fastWrite(uint64_t address, const void* data, size_t len) {
            std::memcpy(reinterpret_cast<void*>(address), data, len);
            return true;
        }

        template<typename T>
        T fastReadValue(uint64_t address) {
            return *reinterpret_cast<T*>(address);
        }

        ElfScanner createScanner(const std::string& name) {
            return ElfScanner::createWithPath(name);
        }
    };
#else
    struct MemBackend {
        pid_t m_pid = 0;
        KittyMemoryMgr m_kittyMemMgr;

        bool init(const std::string& processName) {
            clearError();
            m_pid = KittyMemoryEx::getProcessID(processName);
            if (m_pid < 1) {
                setError(MemError::NotInitialized);
                LOGE("MemBackend::init() error: Failed to get pid: %d", m_pid);
                return false;
            }
            if (!m_kittyMemMgr.initialize(m_pid, EK_MEM_OP_SYSCALL, false)) {
                setError(MemError::NotInitialized);
                LOGE("MemBackend::init() error: failed to init KittyMemoryMgr");
                return false;
            }
            LOGI("MemBackend::init() success, pid: %d, mgr: %p", m_pid, &m_kittyMemMgr);
            return true;
        }

        pid_t getpid() { return m_pid; }

        // 跨进程无 Safe/Fast 区分，统一使用 syscall
        bool safeRead(uint64_t address, void* buffer, size_t len) {
            struct iovec local = { .iov_base = buffer, .iov_len = len };
            struct iovec remote = { .iov_base = reinterpret_cast<void*>(address), .iov_len = len };
            ssize_t ret = (ssize_t)syscall_aarch64(__NR_process_vm_readv, m_pid, &local, 1, &remote, 1, 0, 1);
            if (ret != (ssize_t)len) {
                LOGE("MemBackend:: error: process_vm_readv(%p, %p, %zu) => %zd", address, buffer, len, ret);
                setError(MemError::ReadFailed);
                return false;
            }
            return true;
        }

        bool safeWrite(uint64_t address, void* buffer, size_t len) {
            struct iovec local = { .iov_base = buffer, .iov_len = len };
            struct iovec remote = { .iov_base = reinterpret_cast<void*>(address), .iov_len = len };
            ssize_t ret = (ssize_t)syscall_aarch64(__NR_process_vm_writev, m_pid, &local, 1, &remote, 1, 0, 1);
            if (ret != (ssize_t)len) {
                LOGE("MemBackend:: error: process_vm_writev(%p, %p, %zu) => %zd", address, buffer, len, ret);
                bool ok = IOWrite(address, buffer, len) == (ssize_t)len;
                if (!ok) setError(MemError::IOWriteFailed);
                return ok;
            }
            return true;
        }

        bool fastRead(uint64_t address, void* buffer, size_t len) {
            return safeRead(address, buffer, len);
        }

        bool fastWrite(uint64_t address, const void* data, size_t len) {
            return safeWrite(address, const_cast<void*>(data), len);
        }

        template<typename T>
        T fastReadValue(uint64_t address) {
            T value{};
            safeRead(address, &value, sizeof(T));
            return value;
        }

        ElfScanner createScanner(const std::string& name) {
            return m_kittyMemMgr.findMemElf(name);
        }

        ssize_t IOWrite(uint64_t address, void* buffer, size_t len) {
            if (!IsValid(address)) {
                setError(MemError::InvalidAddress);
                LOGE("MemBackend::IOWrite error: invalid address (%p)", address);
                return -1;
            }
            char memPath[256] = {0};
            snprintf(memPath, sizeof(memPath), "/proc/%d/mem", m_pid);
            int fd = open(memPath, O_RDWR);
            if (fd == -1) {
                setError(MemError::OpenFailed);
                LOGE("MemBackend::IOWrite error: failed to open %s", memPath);
                return -1;
            }
            ssize_t rbytes = pwrite64(fd, buffer, len, address);
            if (rbytes == -1) {
                setError(MemError::IOWriteFailed);
                LOGE("MemBackend::IOWrite error: failed to write to %p", address);
                close(fd);
                return -1;
            }
            close(fd);
            return rbytes;
        }
    };
#endif
} // namespace detail

// ============================================================
//  Mem — platform-agnostic, zero #if defined
//
//  read / write             = Fast (直接 memcpy, 零开销, 默认)
//  safeRead / safeWrite     = Safe (KittyMemory, 遍历 maps)
// ============================================================
class Mem {
public:
    Mem() = default;

    static Mem* instance() {
        static Mem g_instance;
        return &g_instance;
    }

    bool init(const std::string& processName) {
        return m_backend.init(processName);
    }

    pid_t getpid() {
        return m_backend.getpid();
    }

    // ---------- Fast (默认) ----------
    inline bool read(uint64_t address, void* buffer, size_t len) {
        return m_backend.fastRead(address, buffer, len);
    }

    inline bool write(uint64_t address, const void* data, size_t len) {
        return m_backend.fastWrite(address, data, len);
    }

    // ---------- Safe ----------
    inline bool safeRead(uint64_t address, void* buffer, size_t len) {
        return m_backend.safeRead(address, buffer, len);
    }

    inline bool safeWrite(uint64_t address, void* buffer, size_t len) {
        return m_backend.safeWrite(address, buffer, len);
    }

    ElfScanner createScanner(const std::string& name) {
        return m_backend.createScanner(name);
    }

private:
    detail::MemBackend m_backend;
};

#define MemIns Mem::instance()

// ============================================================
//  Free functions — platform-agnostic, zero #if defined
//
//  Read / Write             = Fast (直接 memcpy, 默认)
//  SafeRead / SafeWrite     = Safe (KittyMemory, 遍历 maps)
// ============================================================
inline bool init(const std::string& processName) {
    return MemIns->init(processName);
}

inline pid_t getpid() {
    return MemIns->getpid();
}

inline void elfScan(const std::string& elfName, ElfScanner& scanner) {
    do { std::this_thread::sleep_for(std::chrono::milliseconds(1));
        scanner = MemIns->createScanner(elfName);
    } while (!scanner.isValid());
};

// ---------- Fast (默认) ----------
inline bool Read(uint64_t address, void *buffer, size_t len) {
    return MemIns->read(address, buffer, len);
}

inline bool Write(uint64_t address, const void *data, size_t len) {
    return MemIns->write(address, data, len);
}

template<typename Ret, typename T, typename... Offsets>
inline Ret Read(T base, Offsets... offsets) {
    uint64_t address = (uint64_t)base;
    if (sizeof...(offsets) == 0) {
        Ret value{};
        MemIns->read(address, &value, sizeof(Ret));
        return value;
    }
    std::array<uint64_t, sizeof...(offsets)> offset_array = {static_cast<uint64_t>(offsets)...};
    for (size_t i = 0; i < sizeof...(offsets) - 1; ++i) {
        uint64_t next = 0;
        MemIns->read(address + offset_array[i], &next, sizeof(next));
        address = next;
        if (!IsValid(address)) {
            return Ret{};
        }
    }
    Ret value{};
    MemIns->read(address + offset_array[sizeof...(offsets) - 1], &value, sizeof(Ret));
    return value;
}

template<typename T>
inline typename std::enable_if<
    !std::is_convertible<T, std::string>::value &&
    !std::is_convertible<T, std::vector<uint8_t>>::value,
    bool >::type
Write(uintptr_t address, T value) {
    return Write(address, &value, sizeof(T));
}

inline bool conv_string_to_byte_array(const std::string& hexString, std::vector<uint8_t>& byteArray) {
    if (hexString.size() < 2 || hexString.size() % 2 != 0) return false;
    for (int i = 0; i < hexString.size(); i += 2) {
        int byte = std::stoi(hexString.substr(i, 2), nullptr, 16);
        byteArray.push_back(static_cast<uint8_t>(byte));
    }
    return true;
}

inline bool Write(uintptr_t address, const std::string& hexString) {
    std::vector<uint8_t> byteArray;
    conv_string_to_byte_array(hexString, byteArray);
    return Write(address, byteArray.data(), byteArray.size());
}

inline bool Write(uintptr_t address, const std::vector<uint8_t>& byteArray) {
    return Write(address, (void*)byteArray.data(), byteArray.size());
}

// ---------- Safe ----------
inline bool SafeRead(uint64_t address, void *buffer, size_t len) {
    return MemIns->safeRead(address, buffer, len);
}

inline bool SafeWrite(uint64_t address, void *buffer, size_t len) {
    return MemIns->safeWrite(address, buffer, len);
}

template<typename Ret, typename T, typename... Offsets>
inline Ret SafeRead(T base, Offsets... offsets) {
    uint64_t address = (uint64_t)base;
    if (sizeof...(offsets) == 0) {
        Ret value{};
        MemIns->safeRead(address, &value, sizeof(Ret));
        return value;
    }
    std::array<uint64_t, sizeof...(offsets)> offset_array = {static_cast<uint64_t>(offsets)...};
    for (size_t i = 0; i < sizeof...(offsets) - 1; ++i) {
        uint64_t next = 0;
        if (!MemIns->safeRead(address + offset_array[i], &next, sizeof(next)) || !IsValid(next)) {
            return Ret{};
        }
        address = next;
    }
    Ret value{};
    MemIns->safeRead(address + offset_array[sizeof...(offsets) - 1], &value, sizeof(Ret));
    return value;
}

template<typename T>
inline typename std::enable_if<
    !std::is_convertible<T, std::string>::value &&
    !std::is_convertible<T, std::vector<uint8_t>>::value,
    bool >::type
SafeWrite(uintptr_t address, T value) {
    return SafeWrite(address, &value, sizeof(T));
}

}
