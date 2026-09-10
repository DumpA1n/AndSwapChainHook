#pragma once

#include <functional>

#include "imgui/imgui.h"

enum class GraphicsAPI { OpenGL, Vulkan };

enum class InjectionMode
{
    Overlay,     // Independent overlay window.
    SwapHook,    // hook eglSwapBuffers / vkQueuePresentKHR
};

// ---------------------------------------------------------------------------
// ImGuiHost: host-process ImGui runtime facade.
//
// Two implementations support this namespace:
// Overlay owns a SurfaceView and GPU context through IGraphicsBackend.
// SwapHook intercepts host presentation through SwapChainHook.
//
// Frame driving:
// SwapHook is driven by presentation callbacks and does not require Tick.
// Overlay requires Tick from a stable per-frame source.
// Tick is a no-op in SwapHook mode.
// ---------------------------------------------------------------------------
namespace ImGuiHost
{

struct InitOptions
{
    InjectionMode mode         = InjectionMode::Overlay;
    GraphicsAPI   preferredApi = GraphicsAPI::Vulkan;

    // Render callback invoked after NewFrame and before Render.
    std::function<void()> render;

    // Overlay requires a main-thread task poster to create its SurfaceView.
    // SwapHook does not call this task poster.
    std::function<void(std::function<void()>)> postToMainThread;
};

bool Init(const InitOptions &opt);
void Clean();

// Drive one Overlay frame; no-op in SwapHook mode.
void Tick();

bool          IsInitialized();
InjectionMode GetMode();

// Logical display size normalized to landscape.
// SwapHook returns detected dimensions adjusted for preTransform.
// Overlay returns io.DisplaySize with ScreenMetrics as fallback.
ImVec2 GetDisplaySize();

// Backend switching applies to Overlay; RequestSwitchBackend is a no-op in SwapHook.
// GetCurrentApi returns the recorded value, which may precede API detection.
void        RequestSwitchBackend(GraphicsAPI api);
GraphicsAPI GetCurrentApi();

} // namespace ImGuiHost
