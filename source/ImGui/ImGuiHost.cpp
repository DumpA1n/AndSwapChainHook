#include "ImGuiHost.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <memory>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_android.h"

#include "AndroidPlatform/AndroidPlatform.h"
#include "Graphics/IGraphicsBackend.h"
#include "Graphics/OpenGLBackend.h"
#include "Graphics/SwapChainHook.h"
#include "Graphics/VulkanBackend.h"
#include "ImGuiExtern/ImGuiSoftKeyboard.h"
#include "ImGuiSetup.h"
#include "Logger.h"
#include "Resource/ResourceManager.h"

namespace ImGuiHost
{

// ===========================================================================
// Shared state.
// ===========================================================================
namespace
{

InjectionMode         g_Mode        = InjectionMode::Overlay;
bool                  g_Initialized = false;
std::function<void()> g_RenderCallback;

// Overlay state.
std::unique_ptr<IGraphicsBackend> g_Backend;
GraphicsAPI                       g_CurrentApi    = GraphicsAPI::Vulkan;

bool                              g_PendingSwitch = false;
GraphicsAPI                       g_PendingApi    = GraphicsAPI::Vulkan;

ANativeWindow                    *g_OverlayWindow = nullptr;
ANativeWindow                    *g_PrevAppWindow = nullptr;

// GlobalRef to the host Activity's overlay SurfaceView;
// initialized on the UI thread by OverlayCreateSurfaceViewOnMainThread.
jobject                           g_OverlaySurfaceView = nullptr;

// ===========================================================================
// Create an independent SurfaceView through WindowManager.addView.
// State and JNI helpers are private to ImGuiHost.cpp.
// ===========================================================================

// LayoutParams：TYPE_APPLICATION（2），FLAG_NOT_FOCUSABLE | NOT_TOUCH_MODAL |
// LAYOUT_NO_LIMITS, LAYOUT_IN_SCREEN and HARDWARE_ACCELERATED flags.
jobject CreateOverlayLayoutParams(JNIEnv* env, int width, int height)
{
    jclass lpClass = env->FindClass("android/view/WindowManager$LayoutParams");
    jmethodID lpInit = env->GetMethodID(lpClass, "<init>", "()V");
    jobject lp = env->NewObject(lpClass, lpInit);

    env->SetIntField(lp, env->GetFieldID(lpClass, "type",    "I"), 2);
    env->SetIntField(lp, env->GetFieldID(lpClass, "flags",   "I"),
                     512 | 8 | 16 | 32 | 1024 | 256 | 0x01000000 | 1073741824);
    env->SetIntField(lp, env->GetFieldID(lpClass, "gravity", "I"), 3 | 48);
    env->SetIntField(lp, env->GetFieldID(lpClass, "format",  "I"), 1);
    env->SetIntField(lp, env->GetFieldID(lpClass, "width",   "I"), width);
    env->SetIntField(lp, env->GetFieldID(lpClass, "height",  "I"), height);

    env->DeleteLocalRef(lpClass);
    return lp;
}

// Create a SurfaceView through WindowManager.addView and return a GlobalRef.
jobject CreateAndAttachSurfaceView(JNIEnv* env, jobject activity, int width, int height)
{
    // WindowManager
    jclass actClass = env->GetObjectClass(activity);
    jmethodID getSystemService = env->GetMethodID(actClass,
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring windowStr = env->NewStringUTF("window");
    jobject windowManager = env->CallObjectMethod(activity, getSystemService, windowStr);
    env->DeleteLocalRef(windowStr);

    jclass wmClass = env->GetObjectClass(windowManager);
    jmethodID addView = env->GetMethodID(wmClass, "addView",
        "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");

    // SurfaceView
    jclass svClass = env->FindClass("android/view/SurfaceView");
    jmethodID svInit = env->GetMethodID(svClass, "<init>", "(Landroid/content/Context;)V");
    jobject sv = env->NewObject(svClass, svInit, activity);

    // SurfaceHolder.setFormat(TRANSLUCENT)
    jmethodID getHolder = env->GetMethodID(svClass, "getHolder", "()Landroid/view/SurfaceHolder;");
    jobject surfaceHolder = env->CallObjectMethod(sv, getHolder);
    jclass shClass = env->GetObjectClass(surfaceHolder);
    jmethodID setFormat = env->GetMethodID(shClass, "setFormat", "(I)V");
    env->CallVoidMethod(surfaceHolder, setFormat, -2);

    // addView
    jobject lp = CreateOverlayLayoutParams(env, width, height);
    env->CallVoidMethod(windowManager, addView, sv, lp);

    env->DeleteLocalRef(actClass);
    env->DeleteLocalRef(wmClass);
    env->DeleteLocalRef(windowManager);
    env->DeleteLocalRef(svClass);
    env->DeleteLocalRef(shClass);
    env->DeleteLocalRef(surfaceHolder);
    env->DeleteLocalRef(lp);

    return env->NewGlobalRef(sv);
}

// UI thread only: create the overlay and cache its GlobalRef.
// Posted through InitOptions::postToMainThread.
void OverlayCreateSurfaceViewOnMainThread()
{
    if (g_OverlaySurfaceView) return;

    jobject activity = AndroidPlatform::GetActivity();
    if (!activity)
    {
        LOGE("[ImGuiHost] no Activity available, cannot create overlay");
        return;
    }

    JNIEnv* env = AndroidPlatform::GetJavaEnv();
    if (!env) return;

    AndroidPlatform::DisplaySize size = AndroidPlatform::GetDisplaySize();
    g_OverlaySurfaceView = CreateAndAttachSurfaceView(env, activity, size.width, size.height);
    LOGI("[ImGuiHost] Overlay SurfaceView ready %dx%d view=%p",
         size.width, size.height, g_OverlaySurfaceView);
}

// Acquire ANativeWindow from g_OverlaySurfaceView; the caller must release it.
ANativeWindow* AcquireOverlayWindow()
{
    if (!g_OverlaySurfaceView) return nullptr;

    JNIEnv* env = AndroidPlatform::GetJavaEnv();
    if (!env) return nullptr;

    jclass svClass = env->GetObjectClass(g_OverlaySurfaceView);
    jmethodID getHolder = env->GetMethodID(svClass, "getHolder", "()Landroid/view/SurfaceHolder;");
    jobject surfaceHolder = env->CallObjectMethod(g_OverlaySurfaceView, getHolder);
    env->DeleteLocalRef(svClass);
    if (!surfaceHolder) return nullptr;

    jclass shClass = env->GetObjectClass(surfaceHolder);
    jmethodID getSurface = env->GetMethodID(shClass, "getSurface", "()Landroid/view/Surface;");
    jobject surface = env->CallObjectMethod(surfaceHolder, getSurface);
    env->DeleteLocalRef(shClass);
    env->DeleteLocalRef(surfaceHolder);
    if (!surface) return nullptr;

    ANativeWindow* w = ANativeWindow_fromSurface(env, surface);
    env->DeleteLocalRef(surface);
    return w;
}

// ===========================================================================
// Overlay backend creation and destruction.
// ===========================================================================
std::unique_ptr<IGraphicsBackend> CreateBackend(GraphicsAPI api)
{
    switch (api)
    {
    case GraphicsAPI::Vulkan: return std::make_unique<VulkanBackend>();
    case GraphicsAPI::OpenGL: return std::make_unique<OpenGLBackend>();
    }
    return std::make_unique<OpenGLBackend>();
}

bool OverlayInitBackend(ANativeWindow *window, GraphicsAPI api)
{
    if (g_Backend)
        return true;

    ANativeWindow_acquire(window);

    // Android platform backend initialization.
    // Bind the Android backend to the host input window, not the overlay window.
    ANativeWindow* platformWindow = AndroidPlatform::GetNativeWindow();
    ImGuiSetup::EnsureContext(platformWindow ? platformWindow : window);

    AndroidPlatform::DisplaySize ds = AndroidPlatform::GetDisplaySize();
    g_Backend = CreateBackend(api);
    if (!g_Backend->Init(window, ds.width, ds.height))
    {
        LOGE("[ImGuiHost] backend Init failed");
        g_Backend.reset();
        ImGuiSetup::TeardownContext();
        ANativeWindow_release(window);
        return false;
    }

    ResourceManager::GetInstance().initializeFonts(30.0f);

    ANativeWindow_release(window);
    g_CurrentApi  = api;

    LOGI("[+] ImGuiHost Overlay backend ready  api=%s",
         api == GraphicsAPI::Vulkan ? "Vulkan" : "OpenGL");
    return true;
}

void OverlayTeardownBackend()
{
    if (!g_Backend)
        return;

    g_Backend->Shutdown();
    g_Backend.reset();
    ImGuiSetup::TeardownContext();
}

// ===========================================================================
// Overlay frame driving.
// ===========================================================================
void OverlayDrawFrame()
{
    if (!g_Backend || !g_Backend->IsReady())
        return;

    g_Backend->BeginFrame();
    ImGui_ImplAndroid_NewFrame();

    ImGui::NewFrame();
    ImGuiSoftKeyboard::PreUpdate();

    if (g_RenderCallback)
        g_RenderCallback();

    // Draw the software keyboard before ImGui::Render.
    ImGuiSoftKeyboard::Draw();

    ImGui::Render();

    g_Backend->EndFrame();
}

void OverlayTick()
{
    ANativeWindow* appWindow = AndroidPlatform::GetNativeWindow();

    // Window change: release resources and rebuild on a later frame.
    if (g_PrevAppWindow && appWindow && appWindow != g_PrevAppWindow)
    {
        OverlayTeardownBackend();
        g_OverlayWindow = nullptr;
        g_PrevAppWindow = nullptr;
    }

    // Backend change: release resources and rebuild with pendingApi.
    if (g_PendingSwitch)
    {
        OverlayTeardownBackend();
        g_OverlayWindow = nullptr;
        g_CurrentApi    = g_PendingApi;
        g_PendingSwitch = false;
    }

    // Wait until the UI thread has created the SurfaceView.
    if (!g_OverlayWindow)
    {
        if (!g_OverlaySurfaceView)
            return;

        g_OverlayWindow = AcquireOverlayWindow();
        if (!g_OverlayWindow)
            return;
        g_PrevAppWindow = appWindow;

        if (!OverlayInitBackend(g_OverlayWindow, g_CurrentApi))
        {
            g_OverlayWindow = nullptr;
            g_PrevAppWindow = nullptr;
            return;
        }
    }

    // Draw the frame.
    OverlayDrawFrame();
}

} // anonymous namespace

// ===========================================================================
// Public API.
// ===========================================================================

bool Init(const InitOptions &opt)
{
    if (g_Initialized)
    {
        LOGW("[ImGuiHost] Init called twice");
        return false;
    }

    g_Mode           = opt.mode;
    g_RenderCallback = opt.render;
    g_CurrentApi     = opt.preferredApi;

    if (opt.mode == InjectionMode::SwapHook)
    {
        SwapChainHook::SetRenderCallback(opt.render);
        SwapChainHook::Install();
        g_Initialized = true;
        LOGI("[+] ImGuiHost::Init  mode=SwapHook");
        return true;
    }

    // Overlay requires a host Activity to attach its SurfaceView.
    if (!AndroidPlatform::GetActivity())
    {
        LOGE("[ImGuiHost] Overlay mode requires an Activity, aborting Init");
        return false;
    }

    // Post SurfaceView creation to the main thread.
    if (!opt.postToMainThread)
    {
        LOGE("[ImGuiHost] Overlay mode requires postToMainThread");
        return false;
    }
    opt.postToMainThread([] { OverlayCreateSurfaceViewOnMainThread(); });

    g_Initialized = true;
    LOGI("[+] ImGuiHost::Init  mode=Overlay  preferredApi=%s",
         opt.preferredApi == GraphicsAPI::Vulkan ? "Vulkan" : "OpenGL");
    return true;
}

void Clean()
{
    if (!g_Initialized)
        return;

    if (g_Mode == InjectionMode::SwapHook)
    {
        SwapChainHook::Uninstall();
    }
    else
    {
        OverlayTeardownBackend();
        g_OverlayWindow = nullptr;
        g_PrevAppWindow = nullptr;
        g_PendingSwitch = false;
    }

    g_RenderCallback = nullptr;
    g_Initialized    = false;

    LOGI("[+] ImGuiHost::Clean");
}

void Tick()
{
    if (!g_Initialized)
        return;

    // SwapHook drives its own frames; external Tick is a no-op.
    if (g_Mode == InjectionMode::SwapHook)
        return;

    OverlayTick();
}

bool IsInitialized()
{
    if (!g_Initialized)
        return false;
    if (g_Mode == InjectionMode::SwapHook)
        return SwapChainHook::IsInitialized();
    return g_Backend != nullptr && g_Backend->IsReady();
}

InjectionMode GetMode() { return g_Mode; }

ImVec2 GetDisplaySize()
{
    if (g_Mode == InjectionMode::SwapHook)
    {
        int w = SwapChainHook::GetWidth();
        int h = SwapChainHook::GetHeight();
        if (w > 0 && h > 0)
            return ImVec2((float)w, (float)h);
    }

    if (ImGui::GetCurrentContext())
    {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        if (ds.x > 0 && ds.y > 0)
            return ds;
    }

    AndroidPlatform::DisplaySize ds = AndroidPlatform::GetDisplaySize();
    return ImVec2((float)ds.width, (float)ds.height);
}

void RequestSwitchBackend(GraphicsAPI api)
{
    if (g_Mode != InjectionMode::Overlay)
        return;
    if (api == g_CurrentApi)
        return;
    g_PendingSwitch = true;
    g_PendingApi    = api;
}

GraphicsAPI GetCurrentApi() { return g_CurrentApi; }

} // namespace ImGuiHost
