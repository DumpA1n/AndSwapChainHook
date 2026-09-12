#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace AndSwapChainHook::Assembler
{
enum class Error
{
    None,
    InvalidInput,
    OpenFailed,
    AssemblyFailed
};
struct Result
{
    Error error = Error::None;
    int backendError = 0;
    std::size_t statements = 0;
    std::vector<unsigned char> bytes;
    explicit operator bool() const
    {
        return error == Error::None;
    }
};
// Input is bounded to 1 MiB; failures never return partial machine code.
// Calls serialize the bundled Keystone engine's shared initialization state.
Result TryAsm(const std::string &assembly);
// Vector-returning integration API. A backend failure throws std::runtime_error.
std::vector<unsigned char> Asm(const std::string &assembly);
namespace detail
{
inline std::string HexBytes(const uint8_t *bytes, std::size_t count, std::size_t padding)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result = ".BYTE ";
    for (std::size_t i = 0; i < count + padding; ++i)
    {
        if (i)
            result += ", ";
        const auto value = i < count ? bytes[i] : uint8_t{0};
        result += "0x";
        result += digits[value >> 4];
        result += digits[value & 15];
    }
    return result;
}
} // namespace detail
// Emit the native object representation, padded to four bytes.
template <typename T, typename = std::enable_if_t<!std::is_same_v<T, std::string>>>
std::string Data2Hex(const T &data)
{
    static_assert(std::is_trivially_copyable_v<T>, "Data2Hex requires a trivially copyable object");
    return detail::HexBytes(reinterpret_cast<const uint8_t *>(&data), sizeof(T),
                            (4 - sizeof(T) % 4) % 4);
}
// Strings receive a zero terminator and padding through the next four-byte boundary.
inline std::string Data2Hex(const std::string &value)
{
    return detail::HexBytes(reinterpret_cast<const uint8_t *>(value.data()), value.size(),
                            4 - value.size() % 4);
}
} // namespace AndSwapChainHook::Assembler
