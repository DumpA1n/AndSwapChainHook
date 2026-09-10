# Research scope

AndSwapChainHook is intended for general Android analysis and instrumentation on owned or explicitly authorized systems. This document defines intended research and maintenance scope. The source does not enforce target authorization, and repository visibility or a package profile is not evidence of permission.

## Capability inventory

Source anchors: [Library.cpp](../source/Library.cpp), [SwapChainHook.cpp](../source/Graphics/SwapChainHook.cpp), [InputEventHook.cpp](../source/InputEvent/InputEventHook.cpp).

| Capability | Implementation or entry | Side effects and limits |
|---|---|---|
| Presentation hooks | EGL/Vulkan installation and callbacks | Modifies host control flow; restoration and synchronization defects remain |
| Independent overlay | ImGuiHost, NativeWindowProvider, graphics backends | JNI/window and GPU resources require lifecycle control; standalone driving is incomplete |
| Input interception | InputEventHook | Uses private interfaces and observes app input; threading and acknowledgment are not fully validated |
| Memory and ELF utilities | KittyEx, ElfScanner, PointerHook copy | Reads and writes native memory; can change protections or dumpability |
| Load/start | Constructor and JNI_OnLoad | Installs process signal handlers; key 1337 starts a detached worker and is not authentication |
| Concealment helpers | HookUtils.h and external injector interface | Presence does not establish invocation; external injector exposes memory-backed loading, hiding and watching |

“Source present” means code is in the checkout. “Built” means a specific target compiled and linked. “Executed” requires a recorded runtime test with a target identity. These states are not interchangeable.

## Research boundary

A test record should identify the owner or authorization basis, exact application/build and device, permitted operations, output directory, data disclosure limits and stop condition. Third-party package names alone do not satisfy that record. The current inventory does not assert permission for any third-party application.

Maintained research examples should use owned fixtures. Unauthorized access, credential theft, data exfiltration, destructive activity, and bypassing protective controls on systems outside an authorized assessment are outside the intended maintenance scope. Existing dual-use functionality is disclosed above; this statement does not remove or disable it.

## Current gaps

[README](../README.md) lists unresolved correctness and lifecycle defects. Runtime target restrictions, inert loading, safe shutdown, complete export controls and a versioned owned-app demonstration are development goals where absent, not current guarantees. The repository has no claimed CVP/TAC approval or software security certificate.
