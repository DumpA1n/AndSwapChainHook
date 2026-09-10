#pragma once

#include <array>
#include <set>
#include <string>

#include "Utils/KittyEx.h"

// ============================================================
// X-macro library registry.
// Format: ELF_LIB_ENTRY(enum_name, accessor_name, "library.so").
// ============================================================
#define ELF_LIB_LIST                                                    \
    ELF_LIB_ENTRY(C,               c,               "libc.so")               \
    ELF_LIB_ENTRY(CRYPTO,          crypto,          "libcrypto.so")          \
    ELF_LIB_ENTRY(UE4,             UE4,             "libUE4.so")             \
    ELF_LIB_ENTRY(UNITY,           unity,           "libunity.so")           \
    ELF_LIB_ENTRY(IL2CPP,          il2cpp,          "libil2cpp.so")          \
    ELF_LIB_ENTRY(TERSAFE,         tersafe,         "libtersafe.so")         \
    ELF_LIB_ENTRY(GAME,            game,            "libgame.so")            \
    ELF_LIB_ENTRY(VULKAN,          vulkan,          "libvulkan.so")          \
    ELF_LIB_ENTRY(INPUT,           input,           "libinput.so")           \
    ELF_LIB_ENTRY(ART,             art,             "libart.so")             \
    ELF_LIB_ENTRY(ANDROID_RUNTIME, android_runtime, "libandroid_runtime.so")

class ElfScannerManager {
public:
    // Generate the enum from the registry.
    enum Lib : int {
#define ELF_LIB_ENTRY(ENUM, FUNC, SO) LIB_##ENUM,
        ELF_LIB_LIST
#undef ELF_LIB_ENTRY
        LIB_COUNT
    };

    static ElfScannerManager& GetInstance() {
        static ElfScannerManager g_Instance;
        return g_Instance;
    }

    ElfScannerManager(const ElfScannerManager&) = delete;
    ElfScannerManager& operator=(const ElfScannerManager&) = delete;

    /**
 * Scan the requested libraries asynchronously. libraries contains shared-library names. The
 * current implementation's success result does not validate unknown catalog entries.
 *
 */
    bool Scan(const std::set<std::string>& libraries);

    // Generate O(1) array-index accessors without locking.
#define ELF_LIB_ENTRY(ENUM, FUNC, SO) \
    ElfScanner& FUNC() { return m_scanners[LIB_##ENUM]; }
    ELF_LIB_LIST
#undef ELF_LIB_ENTRY

private:
    ElfScannerManager() = default;
    ~ElfScannerManager() = default;

    std::array<ElfScanner, LIB_COUNT> m_scanners{};
};

// Global access alias.
inline ElfScannerManager& Elf = ElfScannerManager::GetInstance();
