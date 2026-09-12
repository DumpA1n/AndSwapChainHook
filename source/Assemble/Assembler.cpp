#include <AndSwapChainHook/Assembler.h>

#include "keystone/keystone.h"
#include <memory>
#include <mutex>
#include <stdexcept>

namespace AndSwapChainHook::Assembler
{
namespace
{
std::mutex engineOperations;
}

Result TryAsm(const std::string &asmcode)
{
    Result result;
    if (asmcode.empty() || asmcode.size() > 1024 * 1024 || asmcode.find('\0') != std::string::npos)
    {
        result.error = Error::InvalidInput;
        return result;
    }
    std::lock_guard lock(engineOperations);
    ks_engine *raw = nullptr;
    const auto opened = ks_open(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, &raw);
    if (opened != KS_ERR_OK || raw == nullptr)
    {
        result.error = Error::OpenFailed;
        result.backendError = static_cast<int>(opened);
        return result;
    }
    const std::unique_ptr<ks_engine, decltype(&ks_close)> engine(raw, ks_close);
    unsigned char *encoded = nullptr;
    size_t size = 0;
    const int assembled = ks_asm(raw, asmcode.c_str(), 0, &encoded, &size, &result.statements);
    const std::unique_ptr<unsigned char, decltype(&ks_free)> bytes(encoded, ks_free);
    if (assembled != 0 || encoded == nullptr || size == 0)
    {
        result.error = Error::AssemblyFailed;
        result.backendError = static_cast<int>(ks_errno(raw));
        return result;
    }
    result.bytes.assign(encoded, encoded + size);
    return result;
}

std::vector<unsigned char> Asm(const std::string &asmcode)
{
    auto result = TryAsm(asmcode);
    if (!result)
        throw std::runtime_error("AArch64 assembly failed (backend error " +
                                 std::to_string(result.backendError) + ")");
    return std::move(result.bytes);
}

} // namespace AndSwapChainHook::Assembler
