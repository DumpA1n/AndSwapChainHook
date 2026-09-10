# Lab guide

## Source and build baseline

The review baseline is `90fd30add8d9b14a94489fda5160d13ea7decd30`. Build commands are in [README](../README.md). The review used Android NDK r29 (`29.0.14206865`), Clang 21, CMake/Ninja, C++20 and `arm64-v8a`, with API 27 selected by the project. Dependency pins are in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

Default Release compilation and linking passed in an isolated source snapshot. No Android device execution was performed in that review. Build results do not establish runtime compatibility, safe unloading or absence of network activity.

## Runtime gap

This revision has no owned-app harness demonstrating host EGL/Vulkan interception, independent overlay, input acknowledgment and shutdown. The standalone entry has known dispatcher/frame-driving defects. No injection or device command is presented as a validated walkthrough.

A later owned fixture should record host rendering before/after installation, failed installation and rollback, continued host operation after stop, input-thread ownership, window replacement and Vulkan validation results. EGL hook restoration and the present semaphore chain must be repaired before claiming these checks pass.

## Evidence requirements

A runtime record should identify the source/dependency revisions, toolchain/options, fixture owner or permission basis, application build, device/API/GPU, operations performed, expected result, actual result and retained artifact hashes. Failed, skipped and unexecuted checks remain separate states. Raw target data and identity documents do not belong in a public repository.
