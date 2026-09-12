# Lab guide

The implementation tests link public production targets. CMake presets select host Debug, Release, ASan/UBSan and Android AArch64/API 27 Debug/Release. The Android toolchain used for the recorded checks is NDK `29.0.14206865`, Clang 21. Release symbols are retained.

## Executed contracts

| Check | Result and supported conclusion |
|---|---|
| Host Debug/Release/ASan+UBSan CTest | Bounded scan tail reads, cross-chunk matches, short reads, permission errors, corruption, overflow, deadlines, cancellation, budgets, module reload epoch; transactional hook failure/rollback/restore retry; callback replacement, self-stop, concurrent drain and exceptions |
| Independent consumers | `add_subdirectory()` and installed `find_package()` consumers compile and run without AndCommon or Android graphics |
| Android native contracts | Production `/proc/self/mem` reads, denied/unmapped regions, explicit session ownership and repeated scan/start/stop snapshots execute in an owned process |
| Default DSO loading | 50 `dlopen`/`JNI_OnLoad`/`dlclose` cycles preserve threads, file descriptors, current-directory entries, dumpability and five signal handlers |
| Owned EGL pbuffer | 10 source-built Dobby install/render/callback-stop/restore/present cycles on owned presentation entries calling system EGL; borrowed EGL binding restoration; system PACIASP exports explicitly rejected |
| Production Dobby failures | Link-time mprotect failure injection, partial install and failed rollback retention/retry, foreign bytes/permissions refusal, exact per-page RX/RWX restoration and bounded capacity exhaustion |
| Input foundation | Whole-gesture ownership, multiple pointers, saturation/reset, generation changes, late UP, thread-role rejection and 10,000 concurrent DOWN/UP cycles |
| Optional CrashRecorder | Forked record/redelivery, invalid descriptor, repeated install, external-retirement requirement and foreign-handler ownership rejection |
| Owned SurfaceView APK | 10 create/lease/retire/destroy cycles and one Activity recreation; normal WindowManagerGlobal count +1 while attached and baseline after removal; software presentation through a lease; zero refs/windows/leases after retirement |

The recorded device is PLQ110, Android API 36, build `PLQ110_16.0.3.504(CN01)`, AArch64, 4,096-byte pages. EGL reports 1.5/Android; the renderer is Adreno 830, OpenGL ES 3.2 driver `V@0800.60`. System EGL exports on this device begin with PACIASP; only owned entries without PAC are instrumented. This is not coverage for another driver, 16 KiB pages, MTE, BTI/PAC, ShadowCallStack, Vulkan synchronization or concurrent instruction patching.

## Reproduction

Host commands are in [README](../README.md). Native executables are `andswapchain_contract_tests`, `andswapchain_load_test` and, with `ANDSWAPCHAIN_BUILD_GRAPHICS=ON`, `andswapchain_egl_test`. Copy these owned fixtures and the matching DSO into a dedicated device directory. The load fixture accepts the DSO path as its only argument. Retain complete stdout, exit status and hashes together; target construction alone is not a test pass.

The SurfaceView fixture requires SDK platform/build-tools 34 and a JDK:

```sh
export NDK_HOME=/path/to/android-ndk
export ANDROID_SDK_ROOT=/path/to/android-sdk
export JAVA_HOME=/path/to/jdk
python3 tests/overlay/run.py --device DEVICE_SERIAL
```

Omitting `--device` builds the APK without installing it. The script installs only `com.andswapchain.owned`, starts its Activity and reads its private fixture result. It does not modify device settings or inspect another application. The test reads WindowManagerGlobal counts for assertions; the production provider does not modify those records. Failed/missing results make the script fail.

## Open acceptance gates

R01/R05 have bounded transaction and callback tests plus one owned EGL execution path. External address retirement remains a caller guarantee. R07 coverage is borrowed EGL binding preservation, not the retained standalone OpenGL backend. R08 has the explicit SurfaceView provider; automatic lifecycle/executor adaptation, rotation, background stress and input acknowledgment remain open. R04 Vulkan synchronization and Android input-provider/ImGui queue integration are not verified. R16 has the opt-in fixed-record component; full alternate-stack and fault/hardening matrices remain open. R14/R17/R18/R20 auxiliary migration, complete downstream migration, complete dependency inventory and the full release matrix remain incomplete. R22 has a fixed public Dobby source, local patch and matching artifact/symbol evidence; optional source outside the target graph retains its inventory gaps.

C02/C04/C05/C07 have concrete implementation and scoped device evidence; E01/E03/E04 have the checks described above. They are not blanket passes for E01–E08, a repository security certificate or platform approval.
