#include <AndSwapChainHook/Memory.h>

#include <algorithm>
#include <limits>

namespace AndSwapChainHook::Memory
{
ScanResult Scan(const Reader &reader, std::uintptr_t address, std::size_t length,
                std::span<const std::byte> pattern, const ScanOptions &options)
{
    ScanResult result;
    if (length > std::numeric_limits<std::uintptr_t>::max() - address)
    {
        result.error = Error::AddressOverflow;
        return result;
    }
    if (pattern.empty() || pattern.size() > 65536 || options.chunkSize == 0 ||
        options.maxMatches == 0 || pattern.size() > options.byteBudget)
    {
        result.error = Error::InvalidArgument;
        return result;
    }
    const auto fail = [&result](Error error)
    {
        result.error = error;
        return result;
    };
    std::vector<std::byte> buffer;
    std::size_t consumed = 0;
    while (consumed < length)
    {
        if ((options.cancellation && options.cancellation->load(std::memory_order_acquire)))
            return fail(Error::Cancelled);
        if (std::chrono::steady_clock::now() >= options.deadline)
            return fail(Error::DeadlineExceeded);
        const auto remainingBudget = options.byteBudget - result.bytesRead;
        if (!remainingBudget)
            return fail(Error::BudgetExceeded);
        const auto count = std::min({length - consumed, options.chunkSize, remainingBudget});
        const auto retained = buffer.size();
        if (count > buffer.max_size() - retained)
            return fail(Error::AddressOverflow);
        buffer.resize(retained + count);
        auto read = reader.Read(address + consumed, std::span(buffer).subspan(retained, count));
        if (read.bytesRead > count)
            return fail(Error::InvalidArgument);
        result.bytesRead += read.bytesRead;
        if (!read || read.bytesRead != count)
        {
            result.systemError = read.systemError;
            return fail(read ? Error::ShortRead : read.error);
        }
        if ((options.cancellation && options.cancellation->load(std::memory_order_acquire)))
            return fail(Error::Cancelled);
        if (std::chrono::steady_clock::now() >= options.deadline)
            return fail(Error::DeadlineExceeded);
        const auto searchBase = address + consumed - retained;
        for (std::size_t i = 0; i + pattern.size() <= buffer.size(); ++i)
        {
            if ((i & 255) == 0)
            {
                if (options.cancellation && options.cancellation->load(std::memory_order_acquire))
                    return fail(Error::Cancelled);
                if (std::chrono::steady_clock::now() >= options.deadline)
                    return fail(Error::DeadlineExceeded);
            }
            if (std::equal(pattern.begin(), pattern.end(), buffer.begin() + i))
            {
                if (result.matches.size() == options.maxMatches)
                    return fail(Error::BudgetExceeded);
                result.matches.push_back(searchBase + i);
            }
        }
        consumed += count;
        const auto keep = std::min(pattern.size() - 1, buffer.size());
        buffer.erase(buffer.begin(), buffer.end() - keep);
    }
    return result;
}
} // namespace AndSwapChainHook::Memory
