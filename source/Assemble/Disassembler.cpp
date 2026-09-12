#include <AndSwapChainHook/Disassembler.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace AndSwapChainHook
{
size_t Disassembler::ReadProcessMemory(uintptr_t address, void *buffer, size_t size)
{
#if defined(__linux__)
    iovec local{buffer, size};
    iovec remote{reinterpret_cast<void *>(address), size};
    const auto count = syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0);
    return count > 0 ? static_cast<size_t>(count) : 0;
#else
    return 0;
#endif
}

bool Disassembler::Decode(const uint8_t *code, size_t size, uintptr_t address,
                          const CallBack &callback)
{
    csh handle = 0;
    result_.backendError = cs_open(CS_ARCH_AARCH64, CS_MODE_ARM, &handle);
    if (result_.backendError != CS_ERR_OK)
    {
        result_.error = Error::OpenFailed;
        return false;
    }
    struct Engine
    {
        csh *value;
        ~Engine()
        {
            cs_close(value);
        }
    } engine{&handle};
    result_.backendError = cs_option(handle, CS_OPT_DETAIL, details_ ? CS_OPT_ON : CS_OPT_OFF);
    if (result_.backendError != CS_ERR_OK)
    {
        result_.error = Error::OptionFailed;
        return false;
    }
    cs_insn *raw = nullptr;
    const size_t count = cs_disasm(handle, code, size, address, 0, &raw);
    struct Instructions
    {
        cs_insn *value;
        size_t count;
        ~Instructions()
        {
            if (value)
                cs_free(value, count);
        }
    } instructions{raw, count};
    if (!count || !raw)
    {
        result_.backendError = cs_errno(handle);
        result_.error = Error::DecodeFailed;
        return false;
    }
    size_t decoded = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (raw[i].size != 4 || raw[i].size > size - decoded)
        {
            result_.error = Error::DecodeFailed;
            return false;
        }
        decoded += raw[i].size;
        if (callback)
            callback(handle, raw[i]);
        if (log_)
            printDetails(handle, raw[i]);
    }
    result_.instructions += count;
    if (decoded != size)
    {
        result_.error = Error::DecodeFailed;
        return false;
    }
    return true;
}

bool Disassembler::Disasm(const std::vector<uint8_t> &code, uintptr_t address,
                          const CallBack &callback)
{
    result_ = {};
    if (code.empty() || code.size() > MaxInput || code.size() % 4 || !batchSize_ ||
        batchSize_ > MaxBatch || batchSize_ % 4)
    {
        result_.error = Error::InvalidInput;
        return false;
    }
    if (code.size() > std::numeric_limits<uintptr_t>::max() - address)
    {
        result_.error = Error::Overflow;
        return false;
    }
    result_.bytesRead = code.size();
    for (size_t offset = 0; offset < code.size();)
    {
        const auto size = std::min(batchSize_, code.size() - offset);
        if (!Decode(code.data() + offset, size, address + offset, callback))
            return false;
        offset += size;
    }
    return true;
}

bool Disassembler::Disasm(uintptr_t address, size_t size, const CallBack &callback)
{
    result_ = {};
    if (!reader_ || !size || size > MaxInput || size % 4 || !batchSize_ || batchSize_ > MaxBatch ||
        batchSize_ % 4)
    {
        result_.error = Error::InvalidInput;
        return false;
    }
    if (size > std::numeric_limits<uintptr_t>::max() - address)
    {
        result_.error = Error::Overflow;
        return false;
    }
    std::vector<uint8_t> buffer(std::min(size, batchSize_));
    for (size_t offset = 0; offset < size;)
    {
        const size_t count = std::min(batchSize_, size - offset);
        const size_t read = reader_(address + offset, buffer.data(), count);
        result_.bytesRead += std::min(read, count);
        if (read != count)
        {
            result_.error = Error::ShortRead;
            return false;
        }
        if (!Decode(buffer.data(), count, address + offset, callback))
            return false;
        offset += count;
    }
    return true;
}

bool Disassembler::DisasmSingle(uintptr_t address, const CallBack &callback)
{
    return Disasm(address, 4, callback);
}

void Disassembler::printDetails(csh, const cs_insn &instruction)
{
    std::printf("0x%llx: %s %s\n", static_cast<unsigned long long>(instruction.address),
                instruction.mnemonic, instruction.op_str);
}

} // namespace AndSwapChainHook
