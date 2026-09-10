# AndSwapChainHook

AndSwapChainHook is an Android analysis and instrumentation project with EGL/Vulkan presentation hooks, an ImGui interface, independent overlay backends, input hooks, and native memory/ELF utilities. Its intended scope is general Android analysis on owned or explicitly authorized applications.

## Current capabilities

| Component | Source | Boundary |
|---|---|---|
| Host presentation | [SwapChainHook.cpp](source/Graphics/SwapChainHook.cpp) | Changes host execution; hook removal and Vulkan synchronization have unresolved defects |
| Independent overlay | [ImGuiHost.h](source/ImGui/ImGuiHost.h) and graphics backends | Requires a valid window and UI/render scheduling |
| Input | [InputEventHook.cpp](source/InputEvent/InputEventHook.cpp) | Uses private Android interfaces; callback threading and event acknowledgment need validation |
| Native analysis | ELF scanner, memory wrappers, bundled PointerHook | Memory access and pointer writes can affect the host |
| Standalone entry | [Library.cpp](source/Library.cpp) | Constructor installs CrashHandler; `JNI_OnLoad` key `1337` starts a detached worker |

The JNI key is an injector convention, not authentication. The worker scans libraries, loads configuration, initializes graphics and input hooks, and can deliberately crash on scan failure. The standalone dispatcher is posted to without initialization, and overlay mode lacks a complete frame driver.

External injection is a deployment dependency. Associated injector options include `--memfd`, `--hide`, and `--watch` for memory-backed loading, concealment and process watching. These are not authorization controls. This revision has no validated owned-app deployment harness.

## Build

Requirements: CMake 3.22.1 or newer, Ninja, Android NDK, initialized submodules. CMake selects `arm64-v8a`, API 27 and C++20.

```sh
export NDK_HOME=/path/to/android-ndk
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/libAndSwapChainHook.so`.

## Known limitations

Default Release compilation and linking passed with NDK r29 in the repository review. Device execution, safe unload, lifecycle stress, input compatibility and Vulkan validation were not performed in that review.

EGL hooks are not consistently recorded for restoration, while original function pointers are cleared during uninstall. Vulkan present handling consumes host wait semaphores without a complete replacement synchronization chain and mutates caller-owned present data. ImGui callbacks and input delivery lack a demonstrated thread-ownership contract. Native-window lifecycle and crash-signal safety remain unresolved.

AndUEProber pins an older AndSwapChainHook revision. AndCommon contains a separate, diverged copy with provider and lifecycle additions; those private additions are not capabilities of this public checkout.

## Project documentation

- [Research scope](docs/research-scope.md)
- [Lab guide and verification limits](docs/lab-guide.md)
- [Data handling](docs/data-handling.md)
- [Dependency provenance](THIRD_PARTY_NOTICES.md)
- [Security reporting](SECURITY.md)
- [Contributing](CONTRIBUTING.md)

First-party code is [MIT licensed](LICENSE). Research and maintenance guidance does not amend that license. Third-party components retain their own terms.

No repository security certification or platform access approval is claimed.
