#include <AndSwapChainHook/CallbackGate.h>
#include <AndSwapChainHook/Graphics.h>
#include <EGL/eglext.h>
#include <atomic>
#include <dlfcn.h>
#include <dobby.h>
#include <mutex>

namespace
{
using namespace AndSwapChainHook::Instrumentation;
CallbackGate callbacks;
std::mutex operationMutex;
std::recursive_mutex renderMutex;
std::atomic<bool> installed{false};
std::atomic<int> width{0}, height{0};
std::atomic<HookError> callbackError{HookError::None};
thread_local bool inPresent = false;
std::thread::id renderThread;
EGLContext renderContext = EGL_NO_CONTEXT;
EGLBoolean (*originalSwap)(EGLDisplay, EGLSurface) = nullptr;
PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC originalDamage = nullptr;
HookTransaction hooks({nullptr, [](void *, void *target, void *replacement, void **original)
                       { return DobbyHook(target, replacement, original); },
                       [](void *, void *target) { return DobbyDestroy(target); }});

// A callback borrowing the host thread must return its original EGL bindings.
class BindingGuard
{
  public:
    BindingGuard()
        : display_(eglGetCurrentDisplay()), context_(eglGetCurrentContext()),
          draw_(eglGetCurrentSurface(EGL_DRAW)), read_(eglGetCurrentSurface(EGL_READ))
    {
    }
    ~BindingGuard()
    {
        if (eglGetCurrentDisplay() == display_ && eglGetCurrentContext() == context_ &&
            eglGetCurrentSurface(EGL_DRAW) == draw_ && eglGetCurrentSurface(EGL_READ) == read_)
            return;
        const auto display = display_ != EGL_NO_DISPLAY ? display_ : eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY && !eglMakeCurrent(display, draw_, read_, context_))
            callbackError.store(HookError::RestorationFailed);
    }

  private:
    EGLDisplay display_;
    EGLContext context_;
    EGLSurface draw_, read_;
};
void Render(EGLDisplay display, EGLSurface surface)
{
    if (!installed.load(std::memory_order_acquire))
        return;
    std::lock_guard lock(renderMutex);
    if (!installed.load(std::memory_order_acquire) || std::this_thread::get_id() != renderThread ||
        eglGetCurrentContext() != renderContext)
        return;
    EGLint w = 0, h = 0;
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &w) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &h) || w <= 0 || h <= 0)
        return;
    width.store(w);
    height.store(h);
    BindingGuard binding;
    try
    {
        callbacks.Invoke();
    }
    catch (...)
    {
        callbackError.store(HookError::CallbackFailed);
    }
}
class PresentEntry
{
  public:
    PresentEntry() : outer(!inPresent)
    {
        inPresent = true;
    }
    ~PresentEntry()
    {
        if (outer)
            inPresent = false;
    }
    const bool outer;
};
EGLBoolean HookedSwap(EGLDisplay display, EGLSurface surface)
{
    PresentEntry entry;
    if (entry.outer)
        Render(display, surface);
    return originalSwap(display, surface);
}
EGLBoolean HookedDamage(EGLDisplay display, EGLSurface surface, const EGLint *rects, EGLint count)
{
    PresentEntry entry;
    if (entry.outer)
        Render(display, surface);
    return originalDamage(display, surface, rects, count);
}
} // namespace
namespace SwapChainHook
{
Status Install(const InstallOptions &options)
{
    if (options.provider != Provider::EGL)
        return {HookError::UnsupportedProvider};
    if (!options.entriesQuiescent)
        return {HookError::QuiescenceRequired};
    if (options.renderThread == std::thread::id{} || options.context == EGL_NO_CONTEXT)
        return {HookError::InvalidArgument};
    std::lock_guard operation(operationMutex);
    if (hooks.Size())
        return {HookError::AlreadyInstalled, 0, 0, hooks.Size()};
    void *library = nullptr;
    void *swap = reinterpret_cast<void *>(options.functions.swap);
    void *damage = reinterpret_cast<void *>(options.functions.damage);
    if (!swap)
    {
        if (damage)
            return {HookError::InvalidArgument};
        library = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
        if (!library)
            return {HookError::UnsupportedProvider};
        swap = dlsym(library, "eglSwapBuffers");
        damage = dlsym(library, "eglSwapBuffersWithDamageKHR");
        if (!damage)
            damage = reinterpret_cast<void *>(eglGetProcAddress("eglSwapBuffersWithDamageKHR"));
        if (!swap)
        {
            dlclose(library);
            return {HookError::UnsupportedProvider};
        }
    }
    HookRequest requests[2] = {
        {swap, reinterpret_cast<void *>(HookedSwap), reinterpret_cast<void **>(&originalSwap)},
        {damage, reinterpret_cast<void *>(HookedDamage),
         reinterpret_cast<void **>(&originalDamage)}};
    const auto result = hooks.Install(std::span(requests, damage ? 2 : 1), true);
    if (library)
        dlclose(library);
    if (!result)
        return result;
    renderThread = options.renderThread;
    renderContext = options.context;
    callbackError.store(HookError::None);
    callbacks.Start();
    installed.store(true, std::memory_order_release);
    return result;
}
Status StopCallbacks()
{
    installed.store(false, std::memory_order_release);
    return callbacks.Stop() ? Status{} : Status{HookError::CallbacksPending};
}
Status Uninstall(bool entryAddressesRetired)
{
    const auto stopped = StopCallbacks();
    if (!stopped)
        return stopped;
    if (!entryAddressesRetired)
        return {HookError::QuiescenceRequired};
    std::lock_guard operation(operationMutex);
    std::lock_guard render(renderMutex);
    const auto result = hooks.Restore(true);
    if (result)
    {
        width.store(0);
        height.store(0);
        renderContext = EGL_NO_CONTEXT;
        renderThread = {};
    }
    return result;
}
bool IsInitialized()
{
    return installed.load(std::memory_order_acquire);
}
void SetRenderCallback(std::function<void()> callback)
{
    callbacks.Publish(std::move(callback));
}
int GetWidth()
{
    return width.load();
}
int GetHeight()
{
    return height.load();
}
Status LastCallbackStatus()
{
    return {callbackError.load()};
}
} // namespace SwapChainHook
