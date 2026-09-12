#include <AndSwapChainHook/Disassembler.h>
#include <cstring>
#include <iostream>
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value "\n";                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main()
{
    const std::vector<uint8_t> code{0x1f, 0x20, 0x03, 0xd5, 0x1f, 0x20,
                                    0x03, 0xd5, 0xc0, 0x03, 0x5f, 0xd6};
    AndSwapChainHook::Disassembler decoder;
    decoder.set_BatchSize(8);
    std::size_t count = 0;
    bool correct = true;
    CHECK(decoder.Disasm(code, 0x1000,
                         [&](csh, const cs_insn &instruction)
                         {
                             correct =
                                 correct && instruction.address == 0x1000 + count * 4 &&
                                 instruction.size == 4 &&
                                 std::strcmp(instruction.mnemonic, count < 2 ? "nop" : "ret") == 0;
                             ++count;
                         }));
    CHECK(correct && count == 3 && decoder.LastResult().bytesRead == code.size());
    CHECK(!decoder.Disasm(std::vector<uint8_t>{1, 2, 3}, 0x1000));
    CHECK(decoder.LastResult().error == AndSwapChainHook::Disassembler::Error::InvalidInput);
    std::cout << "PASS: independent Capstone-only decoder, exact AArch64 instructions and partial "
                 "batch\n";
}
