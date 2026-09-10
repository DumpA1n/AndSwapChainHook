#include "OpenGLBackend.h"

#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <cstring>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "Logger.h"

bool OpenGLBackend::Init(ANativeWindow *window, int width, int height)
{
    width_ = width;
    height_ = height;

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY)
    {
        LOGE("[OpenGLBackend] eglGetDisplay failed");
        return false;
    }

    eglInitialize(display_, nullptr, nullptr);

    const EGLint egl_attributes[] = {
        EGL_BLUE_SIZE,  8, EGL_GREEN_SIZE, 8,  EGL_RED_SIZE,     8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE};

    EGLint num_configs = 0;
    eglChooseConfig(display_, egl_attributes, nullptr, 0, &num_configs);
    EGLConfig egl_config;
    eglChooseConfig(display_, egl_attributes, &egl_config, 1, &num_configs);
    EGLint egl_format;
    eglGetConfigAttrib(display_, egl_config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(window, width, height, egl_format);

    const EGLint egl_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, egl_config, EGL_NO_CONTEXT, egl_context_attributes);
    surface_ = eglCreateWindowSurface(display_, egl_config, window, nullptr);
    eglMakeCurrent(display_, surface_, surface_, context_);

    // Initialize the ImGui OpenGL renderer.
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Load eglSwapBuffersWithDamageKHR.
    // Select the damage-swap extension when available.
    const char *extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (extensions && strstr(extensions, "EGL_KHR_swap_buffers_with_damage"))
    {
        swapBuffersFn_ = reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC>(
            eglGetProcAddress("eglSwapBuffersWithDamageKHR"));
        if (swapBuffersFn_)
            LOGI("[OpenGLBackend] using eglSwapBuffersWithDamageKHR");
    }
    if (!swapBuffersFn_)
        LOGI("[OpenGLBackend] fallback — no swap extension");

    initialized_ = true;
    LOGI("[OpenGLBackend] initialized %dx%d", width_, height_);
    return true;
}

void OpenGLBackend::BeginFrame()
{
    eglMakeCurrent(display_, surface_, surface_, context_);
    ImGui_ImplOpenGL3_NewFrame();
}

void OpenGLBackend::EndFrame()
{
    ImGuiIO &io = ImGui::GetIO();

    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Present through eglSwapBuffersWithDamageKHR using the full-frame damage region.
    if (swapBuffersFn_)
    {
        EGLint rect[] = {0, 0, (EGLint)io.DisplaySize.x, (EGLint)io.DisplaySize.y};
        swapBuffersFn_(display_, surface_, rect, 1);
    }
}

void OpenGLBackend::Shutdown()
{
    if (!initialized_)
        return;

    ImGui_ImplOpenGL3_Shutdown();

    if (display_ != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (surface_ != EGL_NO_SURFACE)
        {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }

        if (context_ != EGL_NO_CONTEXT)
        {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }

        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }

    initialized_ = false;
    LOGI("[OpenGLBackend] shutdown");
}

bool OpenGLBackend::IsReady() const
{
    return initialized_ && display_ != EGL_NO_DISPLAY;
}
