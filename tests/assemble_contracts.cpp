#include <AndSwapChainHook/Assembler.h>
#include <AndSwapChainHook/Disassembler.h>
#include <array>
#include <cstring>
#include <iostream>
#include <keystone/keystone.h>
#include <stdexcept>
#include <thread>
using namespace AndSwapChainHook;
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value "\n";                                             \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

// External library substitutes inject failures at the production wrapper's API
// boundary. They do not implement the wrapper's ownership or batching logic.
namespace
{
int ksMode = 0, ksHandles = 0, ksAllocations = 0;
int csMode = 0, csHandles = 0, csAllocations = 0;
std::vector<size_t> decodedSizes;
} // namespace
struct ks_struct
{
};
extern "C"
{
    ks_err ks_open(ks_arch, int, ks_engine **engine)
    {
        if (ksMode == 1)
            return KS_ERR_ARCH;
        *engine = new ks_engine;
        ++ksHandles;
        return KS_ERR_OK;
    }
    ks_err ks_close(ks_engine *engine)
    {
        delete engine;
        --ksHandles;
        return KS_ERR_OK;
    }
    ks_err ks_errno(ks_engine *)
    {
        return KS_ERR_ASM_INVALIDOPERAND;
    }
    int ks_asm(ks_engine *, const char *, uint64_t, unsigned char **bytes, size_t *size,
               size_t *count)
    {
        CHECK(ksHandles == 1);
        if (ksMode == 2)
            return -1;
        *bytes = new unsigned char[4]{0xc0, 0x03, 0x5f, 0xd6};
        ++ksAllocations;
        *size = 4;
        *count = 1;
        return ksMode == 3 ? -1 : 0;
    }
    void ks_free(unsigned char *bytes)
    {
        delete[] bytes;
        --ksAllocations;
    }
    cs_err cs_open(cs_arch, cs_mode, csh *handle)
    {
        if (csMode == 1)
            return CS_ERR_ARCH;
        *handle = 1;
        ++csHandles;
        return CS_ERR_OK;
    }
    cs_err cs_close(csh *handle)
    {
        CHECK(*handle == 1);
        --csHandles;
        *handle = 0;
        return CS_ERR_OK;
    }
    cs_err cs_option(csh, cs_opt_type, size_t)
    {
        return csMode == 2 ? CS_ERR_OPTION : CS_ERR_OK;
    }
    cs_err cs_errno(csh)
    {
        return CS_ERR_MEM;
    }
    size_t cs_disasm(csh, const uint8_t *bytes, size_t size, uint64_t address, size_t,
                     cs_insn **output)
    {
        decodedSizes.push_back(size);
        if (csMode == 3)
            return 0;
        // Touch the exact supplied range so ASan observes an out-of-bounds tail.
        for (size_t i = 0; i < size; ++i)
            CHECK(bytes[i] == 0);
        const size_t count = size / 4 + (csMode == 6 ? 1 : 0);
        *output = new cs_insn[count]{};
        ++csAllocations;
        for (size_t i = 0; i < count; ++i)
        {
            (*output)[i].size = csMode == 4 ? 0 : 4;
            (*output)[i].address = address + 4 * i;
        }
        return count;
    }
    void cs_free(cs_insn *output, size_t)
    {
        delete[] output;
        --csAllocations;
    }
}

void analysis_contracts()
{
    CHECK(Assembler::TryAsm("").error == Assembler::Error::InvalidInput);
    for (int mode = 0; mode < 4; ++mode)
    {
        ksMode = mode;
        const auto result = Assembler::TryAsm("ret");
        CHECK(static_cast<bool>(result) == (mode == 0));
        CHECK(ksHandles == 0 && ksAllocations == 0);
    }
    ksMode = 0;
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i)
        workers.emplace_back(
            []
            {
                for (int call = 0; call < 20; ++call)
                    CHECK(Assembler::TryAsm("ret"));
            });
    for (auto &worker : workers)
        worker.join();
    CHECK(ksHandles == 0 && ksAllocations == 0);
    CHECK(Assembler::Data2Hex(std::string{}) == ".BYTE 0x00, 0x00, 0x00, 0x00");
    CHECK(Assembler::Data2Hex(std::string{"A"}) == ".BYTE 0x41, 0x00, 0x00, 0x00");
    CHECK(Assembler::TryAsm(std::string(1024 * 1024 + 1, 'x')).error ==
          Assembler::Error::InvalidInput);
    CHECK(Assembler::TryAsm(std::string("ret\0nop", 7)).error == Assembler::Error::InvalidInput);
    ksMode = 1;
    try
    {
        Assembler::Asm("ret");
        CHECK(false);
    }
    catch (const std::runtime_error &)
    {
    }
    std::array<uint8_t, 12> fixture{};
    std::vector<size_t> reads;
    Disassembler decoder(
        [&](uintptr_t address, void *buffer, size_t size)
        {
            CHECK(address >= 0x1000 && size <= fixture.size() - (address - 0x1000));
            reads.push_back(size);
            std::memcpy(buffer, fixture.data() + address - 0x1000, size);
            return size;
        });
    decoder.set_BatchSize(8);
    size_t callbacks = 0;
    CHECK(decoder.Disasm(0x1000, 12, [&](csh, const cs_insn &) { ++callbacks; }));
    CHECK((reads == std::vector<size_t>{8, 4}) && callbacks == 3);
    CHECK(decoder.LastResult().bytesRead == 12);
    CHECK(decoder.DisasmSingle(0x1000, [&](csh, const cs_insn &) { ++callbacks; }));
    CHECK(callbacks == 4);
    for (int mode = 1; mode < 4; ++mode)
    {
        csMode = mode;
        CHECK(!decoder.DisasmSingle(0x1000));
        CHECK(csHandles == 0 && csAllocations == 0);
    }
    for (int mode : {4, 6})
    {
        csMode = mode;
        CHECK(!decoder.DisasmSingle(0x1000));
        CHECK(decoder.LastResult().error == Disassembler::Error::DecodeFailed);
        CHECK(csHandles == 0 && csAllocations == 0);
    }
    csMode = 0;
    decodedSizes.clear();
    CHECK(decoder.Disasm(std::vector<uint8_t>(12), 0x1000));
    CHECK((decodedSizes == std::vector<size_t>{8, 4}));
    try
    {
        decoder.DisasmSingle(0x1000, [](csh, const cs_insn &) { throw 7; });
        CHECK(false);
    }
    catch (int)
    {
    }
    CHECK(csHandles == 0 && csAllocations == 0);
    decoder.set_BatchSize(0);
    CHECK(!decoder.DisasmSingle(0x1000));
    decoder.set_BatchSize(8);
    CHECK(!decoder.Disasm(UINTPTR_MAX - 3, 8));
    CHECK(decoder.LastResult().error == Disassembler::Error::Overflow);
    Disassembler shortRead([](uintptr_t, void *, size_t count) { return count - 1; });
    CHECK(!shortRead.DisasmSingle(0x1000));
    CHECK(shortRead.LastResult().error == Disassembler::Error::ShortRead);
    CHECK(shortRead.LastResult().bytesRead == 3);
}

int main()
{
    analysis_contracts();
    std::cout
        << "R14: production assembler/disassembler ownership, backend failures, exact tail reads, "
           "callback exceptions, vector batching, concurrent assembly and data encoding passed\n";
}
