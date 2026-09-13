# AndSwapChainHook

AndSwapChainHook provides bounded Android process analysis and optional presentation and SurfaceView components for owned or explicitly authorized applications. Public components do not depend on AndCommon.

## Components

| CMake target | Public header | Contract |
|---|---|---|
| `AndSwapChainHook::Memory` | `AndSwapChainHook/Memory.h` | Explicit process reader with default mapping-permission enforcement and opt-in procfs force-read; mapping, overflow and short-read errors; module identity and generation catalog |
| `AndSwapChainHook::Analysis` | `Memory.h`, `Runtime.h` | Exact byte scanning with budgets, deadlines and cancellation; explicit single-worker sessions and copied result snapshots |
| `AndSwapChainHook::Input` | `Input.h` | Bounded copied events, whole-gesture ownership, explicit producer/consumer roles and generation reset |
| `AndSwapChainHook::Profiler` | `Profiler.h` | [Scoped timing and bounded publication](docs/profiler.md); 96 owned names of at most 95 bytes; atomic counters and history |
| `AndSwapChainHook::Instrumentation` | `HookTransaction.h`, `CallbackGate.h` | Bounded installation records, reverse rollback and restoration failure retention; synchronized callback publication and drain |
| `AndSwapChainHook::Graphics` | `Graphics.h` | Optional Android AArch64 EGL presentation adapter; explicit render owner/context; preserves borrowed EGL bindings |
| `AndSwapChainHook::Overlay` | `Overlay.h` | Optional Android SurfaceView through normal WindowManager ownership; explicit Activity/UI owner, generation leases and nonblocking retirement |
| `AndSwapChainHook::CrashRecorder` | `CrashRecorder.h` | Optional fixed binary signal records; explicit descriptor, disposition and alternate-stack ownership |
| `AndSwapChainHook::HookAdapter` | `pointerhook/Hook.h` from PointerHook | Optional dependency target linking a separately configured `PointerHook::Core` |

The Android `libAndSwapChainHook.so` exports the `ASC_*` analysis session API. `JNI_OnLoad` only returns `JNI_VERSION_1_6`, including when its reserved argument is `1337`. Loading installs no hooks or signal handlers, starts no analysis worker, reads no target memory, loads no configuration and changes no process dumpability or memory permissions. Explicit `ASC_Initialize` opens `/proc/self/mem`; permission denial is returned without selecting another access method. `ASC_Start` copies its pattern and owns one joinable worker. `ASC_Cancel` requests cancellation, `ASC_Stop` joins, and `ASC_Destroy` releases the handle. Destruction requires exclusive handle ownership.

`ProcessReader::Open(pid, MappingReadPolicy::AllowProcfsForceRead)` is an explicit read-only analysis mode for authorized targets. It retains mapped-range and overflow validation but permits `/proc/<pid>/mem` `pread` on mappings without `VM_READ`; the kernel remains the access-control authority. It does not attach, change page permissions, change dumpability or add a write channel. The default policy remains `RequireReadable`, including the `ASC_*` API.

## Build and consume

CMake 3.22.1+, Ninja and C++20 are required. Android presets require Android NDK r29 and select AArch64/API 27; toolchain selection is not imposed on `add_subdirectory()` consumers. Debug symbols are retained in Release artifacts.

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

export NDK_HOME=/path/to/android-ndk
cmake --preset android-release
cmake --build --preset android-release
```

Host Debug, Release and sanitizer presets build the pure components. `ProcessReader` returns `UnsupportedPlatform` outside Linux; host fixtures supply the same `Reader` interface. No submodule is needed by these targets.

`ANDSWAPCHAIN_BUILD_GRAPHICS=ON` builds EGL instrumentation; `ANDSWAPCHAIN_BUILD_OVERLAY=ON` builds the SurfaceView provider. `ANDSWAPCHAIN_BUILD_CRASH_RECORDER=ON` builds the opt-in fixed-record signal sink. These options are off by default and remain separate static targets. Graphics builds the [fixed Dobby source package](cmake/dobby/README.md), or consumes an existing `Dobby::Dobby` target implementing contract version 1; installation includes the default dependency package and license. `ANDSWAPCHAIN_BUILD_POINTER_ADAPTER=ON` requires an existing `PointerHook::Core` target or an installed PointerHook package. The consumer owns its exact dependency revision. There is no bundled PointerHook implementation.

```cmake
add_subdirectory(AndSwapChainHook)
target_link_libraries(my_agent PRIVATE AndSwapChainHook::Analysis)
```

```sh
cmake --install build/host-release --prefix /path/to/install
```

```cmake
find_package(AndSwapChainHook 0.2 CONFIG REQUIRED COMPONENTS Memory Analysis)
target_link_libraries(my_tool PRIVATE AndSwapChainHook::Analysis)
```

## AArch64 assembler and disassembler

`AndSwapChainHook::Assembler` provides `Assembler::TryAsm`, `Assembler::Asm`, and `Assembler::Data2Hex`. `AndSwapChainHook::Disassembler` decodes owned byte vectors or bounded reads through a caller-supplied `Reader`; its public callbacks expose Capstone instruction types. `AndSwapChainHook::Assemble` links both components. These components generate and inspect instruction bytes without executing them.

`ANDSWAPCHAIN_BUILD_ASSEMBLER=ON` opts into the pinned Keystone source package and its GPL version 2/commercial licensing. `ANDSWAPCHAIN_BUILD_DISASSEMBLER=ON` enables the pinned Capstone package independently. `ANDSWAPCHAIN_BUILD_ASSEMBLE=ON` enables both. All three options default to `OFF`; the [dependency notices](THIRD_PARTY_NOTICES.md) record source pins, patches, and installed license paths.

```cmake
find_package(AndSwapChainHook 0.2 CONFIG REQUIRED COMPONENTS Disassembler)
target_link_libraries(my_decoder PRIVATE AndSwapChainHook::Disassembler)
```

The standalone public consumer accepts `-DASSEMBLE_COMPONENT=Assembler`, `Disassembler`, or `Assemble`. A disassembler-only producer package contains no Keystone target; an assembler-only producer package contains no Capstone target. Installed headers and their declared transitive dependencies are sufficient for each consumer.

```sh
cmake -S tests/assemble_consumer -B build/language-consumer -G Ninja \
  -DANDSWAPCHAIN_SOURCE_DIR="$PWD" -DASSEMBLE_COMPONENT=Disassembler
cmake --build build/language-consumer
ctest --test-dir build/language-consumer --output-on-failure
```

`tests/verify_assemble.py` verifies Debug/Release producer builds, source and installed public consumers, header self-containment, exact AArch64 encodings and decode/re-encode round trips. `--ndk /path/to/ndk` adds Android cross-builds; `--device SERIAL` additionally runs only the owned byte fixtures on that selected device. Results and exact source/artifact hashes reside in `tests/verification.json` under `assembly_validation`; this independent evidence does not refresh other component results.

## Operation ownership

`ProcessReader::Open`, `RefreshMappings` and module catalog updates belong to one analysis owner; do not update them concurrently with readers. Module identities include a caller-supplied load epoch so an address reused by a module reload invalidates the generation. The catalog does not infer loader events. Scans allow patterns up to 65,536 bytes and return partial progress with an error when a limit or read failure occurs.

EGL installation requires owned target bytes/page permissions/mapping lifetime, parked entry threads and an explicitly selected render thread and EGL context. `InstallOptions::functions` accepts explicitly owned presentation entries with exact EGL signatures; a null table selects system exports. PAC/BTI entries are rejected. PLQ110 system EGL exports begin with PACIASP and are unsupported; the verified owned entry fixture calls the real system EGL implementation. The adapter does not create ImGui contexts or discover Activities. The caller owns callback GPU resources and GL-state restoration; EGL display/context/read/draw bindings are restored by the adapter. `StopCallbacks` prevents new callbacks and drains active callbacks; callback-initiated stop returns `CallbacksPending` for an external owner to finish.

`Uninstall(true)` requires every previously obtained replacement and original trampoline address to be retired: no such address may execute during restoration or become callable afterwards, including when parked threads resume. Consumer-supplied Dobby implementations can free trampolines during restoration. The default source facade bounds allocations to 256 attempts and retains them until process exit. Zero active callbacks or parked threads alone do not establish this condition. Without that proof, only stop callbacks and keep the patches and DSO resident. DSO unloading additionally requires proof covering all exported handles, workers, callbacks and retained addresses.

SurfaceView `Create`, `Poll`, `Retire` and `Destroy` execute synchronously on the Android UI thread with its current `JNIEnv` and the caller-selected Activity. Rendering uses a move-only generation lease. Retirement rejects new leases and returns `Busy` while a lease remains. Retry after releasing leases; failed destruction preserves the handle. The host must retire a generation on surface destruction, Activity replacement or background lifecycle changes. No WindowManagerGlobal record is removed by the provider.

Input `Queue` copies at most 32 pointers per event into a fixed 64-entry queue. The producer alone submits one ordered physical gesture stream; the consumer alone drains it. Only DOWN selects capture ownership. Overflow or generation invalidation publishes an undroppable synthetic `Action::Reset`; consumers must release all held input state before subsequent events, regardless of generation. A retired gesture retains its host/capture decision through UP/CANCEL, and a second unresolved retiring gesture returns `Busy`. No private InputConsumer hook is installed by this component.

## Verification and remaining gates

[Lab guide](docs/lab-guide.md) records executed host/device checks and their limits. [Verification manifest](tests/verification.json) records options and artifact hashes. The owned SurfaceView APK and native EGL pbuffer fixture are executable tests of public production targets.

The host Vulkan provider returns `UnsupportedProvider`. Vulkan synchronization, Android input-provider/ImGui queue integration, ImGui ownership, automatic Android lifecycle integration and auxiliary module migration are unfinished. Existing `source/ImGui`, `InputEvent`, `AndroidPlatform`, standalone graphics backends, and configuration/ELF utilities are retained source outside the advertised target graph; their previous limitations remain. They are not compatibility targets and must not be compiled by globbing dependency internals. Private downstream migration and complete architecture batches 04–11 remain open.

Link one CrashRecorder component instance per process; separate static copies do not coordinate signal ownership. Each instance permits one installation and retains its duplicated sink for process lifetime. Its handler writes a fixed binary record and redelivers using the saved disposition; it does not symbolize, allocate a report or recover arbitrary C++ execution. Alternate stacks belong to the caller. Restoration requires external signal-address retirement and never grants DSO unload permission.

Dobby source, local patch, toolchain, artifacts and symbols are recorded in the verification manifest. One device and a 4 KiB page configuration do not establish 16 KiB, MTE, BTI/PAC, ShadowCallStack, Vulkan or general safe-unload support.

- [Research scope](docs/research-scope.md)
- [Data handling](docs/data-handling.md)
- [Dependency provenance](THIRD_PARTY_NOTICES.md)
- [Security reporting](SECURITY.md)

First-party code is [MIT licensed](LICENSE). No platform approval, certification or release publication is claimed.
