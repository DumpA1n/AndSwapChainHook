#include <AndSwapChainHook/Assembler.h>
#include <iostream>
#include <stdexcept>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value "\n";                                               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int main()
{
    namespace Assembler = AndSwapChainHook::Assembler;
    const std::vector<unsigned char> expected{0x40, 0x05, 0x80, 0xd2, 0xc0, 0x03, 0x5f, 0xd6};
    const auto assembled = Assembler::TryAsm("mov x0, #42; ret");
    CHECK(assembled && assembled.statements == 2 && assembled.bytes == expected);
    CHECK(Assembler::Asm("mov x0, #42; ret") == expected);
    CHECK(Assembler::Asm(Assembler::Data2Hex(std::string{"ABCD"})) ==
          std::vector<unsigned char>({'A', 'B', 'C', 'D', 0, 0, 0, 0}));
    CHECK(Assembler::TryAsm("").error == Assembler::Error::InvalidInput);
    const auto invalid = Assembler::TryAsm("invalid_assembly_instruction");
    CHECK(!invalid && invalid.bytes.empty() && invalid.backendError != 0);
    bool threw = false;
    try
    {
        Assembler::Asm("invalid_assembly_instruction");
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    CHECK(threw);
    std::cout << "PASS: independent Keystone-only assembler, exact AArch64 bytes, padded data "
                 "and invalid input\n";
}
