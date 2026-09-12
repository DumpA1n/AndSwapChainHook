#pragma once

#include "capstone/capstone.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace AndSwapChainHook
{
// One owner operates a Disassembler instance. Callbacks borrow each instruction
// only for the duration of the call; recursive calls use a separate instance.
class Disassembler
{
  public:
    using CallBack = std::function<void(csh, const cs_insn &)>;
    using Reader = std::function<size_t(uintptr_t, void *, size_t)>;
    enum class Error
    {
        None,
        InvalidInput,
        Overflow,
        ShortRead,
        OpenFailed,
        OptionFailed,
        DecodeFailed
    };
    struct Result
    {
        Error error = Error::None;
        size_t bytesRead = 0;
        size_t instructions = 0;
        cs_err backendError = CS_ERR_OK;
        explicit operator bool() const
        {
            return error == Error::None;
        }
    };

    explicit Disassembler(Reader reader = ReadProcessMemory) : reader_(std::move(reader)) {}
    static Disassembler *instance()
    {
        static Disassembler value;
        return &value;
    }
    bool Disasm(const std::vector<uint8_t> &code, uintptr_t address, const CallBack &callback = {});
    bool Disasm(uintptr_t address, size_t size, const CallBack &callback = {});
    bool DisasmSingle(uintptr_t address, const CallBack &callback = {});
    Result LastResult() const
    {
        return result_;
    }
    bool get_IsDetailsOn() const
    {
        return details_;
    }
    void set_IsDetailsOn(bool value)
    {
        details_ = value;
    }
    bool get_IsLogOn() const
    {
        return log_;
    }
    void set_IsLogOn(bool value)
    {
        log_ = value;
    }
    void set_BatchSize(size_t value)
    {
        batchSize_ = value;
    }
    void printDetails(csh handle, const cs_insn &instruction);

  private:
    static size_t ReadProcessMemory(uintptr_t address, void *buffer, size_t size);
    bool Decode(const uint8_t *code, size_t size, uintptr_t address, const CallBack &callback);
    Reader reader_;
    Result result_;
    bool details_ = true;
    bool log_ = false;
    size_t batchSize_ = 2000;
    static constexpr size_t MaxBatch = 65536;
    static constexpr size_t MaxInput = 16 * 1024 * 1024;
};

} // namespace AndSwapChainHook
