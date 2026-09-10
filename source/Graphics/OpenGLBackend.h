#pragma once

#include "IGraphicsBackend.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

class OpenGLBackend : public IGraphicsBackend
{
public:
    bool Init(ANativeWindow *window, int width, int height) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Shutdown() override;
    bool IsReady() const override;

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;

    // Resolve the damage-swap extension; selecting it does not guarantee concealment.
    PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC swapBuffersFn_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
};
