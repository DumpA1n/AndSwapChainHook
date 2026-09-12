#pragma once

#include <AndSwapChainHook/HookTransaction.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <functional>
#include <thread>

namespace SwapChainHook
{
using Status = AndSwapChainHook::Instrumentation::HookStatus;
enum class Provider
{
    EGL,
    Vulkan
};
struct EGLFunctions
{
    PFNEGLSWAPBUFFERSPROC swap = nullptr;
    PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC damage = nullptr;
};
struct InstallOptions
{
    Provider provider = Provider::EGL;
    // All threads that could enter any patched target must be parked.
    bool entriesQuiescent = false;
    std::thread::id renderThread;
    EGLContext context = EGL_NO_CONTEXT;
    // Optional owned presentation entries with these exact EGL signatures.
    // Null swap resolves system exports; PAC/BTI entries are unsupported.
    EGLFunctions functions;
};
// Does not initialize ImGui, resolve Activities, acquire windows, or install input
// hooks. The host owns callback resources and chooses their context/owner thread.
Status Install(const InstallOptions &options = {});
// Stops new callbacks and waits for current callbacks. Calling from a callback
// disables publication and returns CallbacksPending without waiting on itself.
Status StopCallbacks();
// The host must retire every acquired replacement/original trampoline address:
// none may be executing during removal or become callable after removal, even
// when a parked thread resumes. DobbyDestroy may free its original trampoline.
// Merely parking threads or observing zero active callbacks is insufficient.
// Without this guarantee, use StopCallbacks and keep the patches/DSO resident.
// A successful restoration still requires separate DSO-wide unload proof.
Status Uninstall(bool entryAddressesRetired = false);
bool IsInitialized();
void SetRenderCallback(std::function<void()> callback);
int GetWidth();
int GetHeight();
Status LastCallbackStatus();
} // namespace SwapChainHook
