#pragma once

#include <android/native_window.h>

// Graphics backend interface.
// Shared interface for OpenGL ES and Vulkan.
class IGraphicsBackend
{
public:
    virtual ~IGraphicsBackend() = default;

    // Initialize GPU resources and the ImGui renderer.
    // ImGui::CreateContext must have completed first.
    virtual bool Init(ANativeWindow *window, int width, int height) = 0;

    // Begin a frame: synchronize and call the renderer's NewFrame.
    virtual void BeginFrame() = 0;

    // End a frame: submit drawing and present.
    virtual void EndFrame() = 0;

    // Shut down the renderer and release GPU resources.
    virtual void Shutdown() = 0;

    // Whether the backend is ready.
    virtual bool IsReady() const = 0;
};
