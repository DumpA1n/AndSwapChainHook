#include <AndSwapChainHook/Profiler.h>

#include <cstring>
#include <iostream>
#include <string>

int main()
{
    namespace Prof = AndSwapChainHook::Profiler;
    const auto registration = Prof::Register(std::string{"temporary-name"});
    if (!registration || std::strcmp(Prof::GetZone(registration.index).name, "temporary-name") != 0)
        return 1;
    Prof::SetEnabled(true);
    {
        ANDSWAPCHAIN_PROF_ZONE(std::string{"owned-scope"}); ANDSWAPCHAIN_PROF_ZONE("second-scope");
    }
    if (Prof::ZoneCount() != 3 || Prof::GetZone(1).calls != 1 || Prof::GetZone(2).calls != 1)
        return 2;
    Prof::SetEnabled(false);
    if (!Prof::Record(registration.index, 42) || Prof::GetZone(registration.index).lastNs != 42)
        return 3;
    std::cout << "PASS: public Profiler target, owned temporary names and same-line scoped macros\n";
}
