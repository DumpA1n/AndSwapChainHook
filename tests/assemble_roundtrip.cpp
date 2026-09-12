#include <AndSwapChainHook/Assembler.h>
#include <AndSwapChainHook/Disassembler.h>
#include <cstring>
#include <iostream>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #x "\n";                                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
using namespace AndSwapChainHook;
int main()
{
    auto assembled = Assembler::TryAsm("mov x0, #42; ret");
    CHECK(assembled);
    CHECK(assembled.bytes.size() == 8 && assembled.statements == 2);
    auto emptyData = Assembler::TryAsm(Assembler::Data2Hex(std::string{}));
    CHECK(emptyData && emptyData.bytes == std::vector<unsigned char>(4, 0));
    auto stringData = Assembler::TryAsm(Assembler::Data2Hex(std::string{"ABCD"}));
    CHECK(stringData &&
          stringData.bytes == std::vector<unsigned char>({'A', 'B', 'C', 'D', 0, 0, 0, 0}));
    auto invalid = Assembler::TryAsm("invalid_assembly_instruction");
    CHECK(!invalid && invalid.bytes.empty());
    const unsigned char expected[]{0x40, 0x05, 0x80, 0xd2, 0xc0, 0x03, 0x5f, 0xd6};
    CHECK(memcmp(assembled.bytes.data(), expected, sizeof(expected)) == 0);
    Disassembler decoder;
    std::size_t decoded = 0;
    bool correct = true;
    std::string reconstructed;
    CHECK(decoder.Disasm(assembled.bytes, 0x1000,
                         [&](csh, const cs_insn &instruction)
                         {
                             correct = correct && instruction.size == 4 &&
                                       instruction.address == 0x1000 + decoded * 4;
                             reconstructed += std::string(instruction.mnemonic) + " " +
                                              instruction.op_str + ";";
                             ++decoded;
                         }));
    CHECK(correct && decoded == 2 && decoder.LastResult().instructions == 2);
    CHECK(Assembler::Asm(reconstructed) == assembled.bytes);
    Disassembler shortReader([](uintptr_t, void *, std::size_t) { return std::size_t{0}; });
    CHECK(!shortReader.Disasm(static_cast<uintptr_t>(0x1000), 4));
    CHECK(shortReader.LastResult().error == Disassembler::Error::ShortRead);
    std::cout << "PASS: pinned source Keystone/Capstone round-trip, exact AArch64 bytes, invalid "
                 "assembly and short reader\n";
}
