#include <AndSwapChainHook/Graphics.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value " EGL=" << eglGetError() << '\n';                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
__attribute__((noinline)) EGLBoolean OwnedSwap(EGLDisplay display, EGLSurface surface)
{
    return eglSwapBuffers(display, surface);
}
__attribute__((noinline)) EGLBoolean OwnedDamage(EGLDisplay display, EGLSurface surface,
                                                 const EGLint *, EGLint)
{
    return eglSwapBuffers(display, surface);
}
int main()
{
    using namespace AndSwapChainHook::Instrumentation;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    CHECK(display != EGL_NO_DISPLAY && eglInitialize(display, &major, &minor));
    EGLint attrs[] = {EGL_SURFACE_TYPE,
                      EGL_PBUFFER_BIT,
                      EGL_RENDERABLE_TYPE,
                      EGL_OPENGL_ES3_BIT,
                      EGL_RED_SIZE,
                      8,
                      EGL_GREEN_SIZE,
                      8,
                      EGL_BLUE_SIZE,
                      8,
                      EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    CHECK(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
    EGLint surfaceAttrs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
    EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    CHECK(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
          eglMakeCurrent(display, surface, surface, context));
    std::cout << "EGL " << major << '.' << minor
              << " vendor=" << eglQueryString(display, EGL_VENDOR)
              << " renderer=" << glGetString(GL_RENDERER) << " version=" << glGetString(GL_VERSION)
              << '\n';
    void *library = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    CHECK(library);
    bool pacEntry = false;
    for (const char *name : {"eglSwapBuffers", "eglSwapBuffersWithDamageKHR"})
    {
        if (void *entry = dlsym(library, name))
        {
            uint32_t words[4];
            memcpy(words, entry, sizeof(words));
            pacEntry = pacEntry || words[0] == 0xd503233f || words[0] == 0xd503237f;
            std::cout << name << " entry_words=" << std::hex << words[0] << ',' << words[1] << ','
                      << words[2] << ',' << words[3] << std::dec << '\n';
        }
    }
    dlclose(library);
    std::atomic<int> calls = 0;
    CHECK(SwapChainHook::Install().error == HookError::QuiescenceRequired);
    CHECK(SwapChainHook::Install({.provider = SwapChainHook::Provider::Vulkan}).error ==
          HookError::UnsupportedProvider);
    const auto options = SwapChainHook::InstallOptions{.entriesQuiescent = true,
                                                       .renderThread = std::this_thread::get_id(),
                                                       .context = context,
                                                       .functions = {OwnedSwap, OwnedDamage}};
    if (pacEntry)
    {
        auto systemOptions = options;
        systemOptions.functions = {};
        const auto systemResult = SwapChainHook::Install(systemOptions);
        CHECK(systemResult.error == HookError::InstallationFailed &&
              systemResult.backendError == -ENOTSUP && systemResult.retainedHooks == 0);
        std::cout << "System EGL PAC entries: rejected; system export hooking is unsupported\n";
    }
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        SwapChainHook::SetRenderCallback(
            [&]
            {
                ++calls;
                if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
                    std::abort();
            });
        auto installed = SwapChainHook::Install(options);
        if (!installed)
            std::cerr << "install error=" << static_cast<int>(installed.error)
                      << " backend=" << installed.backendError << '\n';
        CHECK(installed);
        const auto previous = calls.load();
        CHECK(OwnedSwap(display, surface));
        CHECK(calls == previous + 1 && eglGetCurrentContext() == context &&
              eglGetCurrentSurface(EGL_DRAW) == surface &&
              eglGetCurrentSurface(EGL_READ) == surface);
        CHECK(SwapChainHook::GetWidth() == 16 && SwapChainHook::GetHeight() == 16 &&
              SwapChainHook::LastCallbackStatus());
        SwapChainHook::SetRenderCallback(
            [&]
            {
                ++calls;
                if (SwapChainHook::StopCallbacks().error != HookError::CallbacksPending)
                    std::abort();
            });
        CHECK(OwnedSwap(display, surface));
        CHECK(calls == previous + 2);
        CHECK(SwapChainHook::StopCallbacks());
        CHECK(OwnedSwap(display, surface) && calls == previous + 2);
        // This owned fixture is the only entry caller and retains no hook or
        // original-trampoline address when restoration begins.
        CHECK(SwapChainHook::Uninstall(true));
        CHECK(OwnedSwap(display, surface) && calls == previous + 2);
    }
    CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(display, context) && eglDestroySurface(display, surface) &&
          eglTerminate(display));
    std::cout << "R01/R05/R07: 10 owned presentation entry/EGL pbuffer "
                 "install/render/callback-stop/restore/present "
                 "cycles passed\n";
}
