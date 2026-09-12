# Profiler component

`AndSwapChainHook::Profiler` is a default static C++20 component for scoped timing and bounded publication of named statistics. Its public header, `<AndSwapChainHook/Profiler.h>`, depends only on the C++ standard library. The target declares its thread-library dependency and requires no Common, Logger, ImGui, or Android headers.

```cmake
find_package(AndSwapChainHook 0.2 CONFIG REQUIRED COMPONENTS Profiler)
target_link_libraries(my_tool PRIVATE AndSwapChainHook::Profiler)
```

```cpp
namespace Prof = AndSwapChainHook::Profiler;
const auto zone = Prof::Register(std::string{"temporary name"});
if (zone)
    Prof::Record(zone.index, 1200);
Prof::SetEnabled(true);
{
    ANDSWAPCHAIN_PROF_ZONE("operation");
}
```

The registry owns at most 96 names, each containing 1–95 bytes plus a zero terminator. Registration copies the name before publishing its index. Temporary strings are valid for the duration of the registration call; no caller-owned name buffer is retained. Empty names, null C strings, and embedded zero bytes return `InvalidName`; longer names return `NameTooLong` without truncation. A full registry returns `CapacityExhausted`. Failed registration has index `-1` and consumes no slot. Duplicate names use distinct slots, indices are never reused, and all returned name pointers remain valid while the component instance exists.

`ZoneCount` and `TryGetZone` acquire published registry entries. `TryGetZone` returns null for invalid or unpublished indices; `GetZone` throws `std::out_of_range` for those indices. `Record` returns false for an invalid index. A macro expansion registers once, including when timing is disabled, and accepts temporary names. Macro registration failures disable that scope; explicit `Register` exposes their error.

Calls, total nanoseconds, minimum, maximum, last duration, and 128 microsecond history elements are atomic. Reading several fields is not a coherent snapshot. Concurrent writers do not guarantee chronological history ordering; unsigned counters wrap according to their integer type. `Reset` clears counters individually while retaining registered names, history samples, and the history cursor. A clean measurement interval requires the caller to stop recording before reset. An enabled `ScopedZone` records at destruction; disabling timing during an existing scope does not cancel that scope.

The registry belongs to the linked component instance. Independent static copies in separate images have separate registries. The public API does not coordinate their state.

Common's `Utils/Profiler.h` forwards the `Prof::` types and functions and `PROF_*` macros to this target; `Prof::LogDump` is its Logger sink. Consumers must link `AndSwapChainHook::Profiler`. Empty and overlong names are rejected; direct users of private `Prof::detail` storage have no compatibility contract. `Prof::detail::Register` and `Record` are forwarding entry points, with the integer registration API returning `-1` on failure.

`tests/profiler.cpp` covers owned names, publication during concurrent registration, exact capacity, counter updates by eight writers, scoped timing, and reset/history behavior. `tests/profiler_consumer` validates source and installed public consumption and header self-containment. [profiler-verification.json](../tests/profiler-verification.json) records the exact source, artifacts, commands, and execution limits independently from other component evidence.
