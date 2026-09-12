#include <atomic>
#include <memory>
#include <pointerhook/Hook.h>
int main()
{
    std::atomic<std::uintptr_t> value{0x1000};
    pointerhook::AtomicSlot slot(value);
    std::uintptr_t original = 0;
    if (!slot.Load(original) || original != 0x1000)
        return 1;
    if (!slot.CompareExchange(original, 0x2000))
        return 2;
    if (!slot.CompareExchange(0x2000, original) || value != 0x1000)
        return 3;
    return pointerhook::Signature::For<float, double>().supported ? 0 : 4;
}
