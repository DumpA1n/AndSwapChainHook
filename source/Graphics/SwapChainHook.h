#pragma once

#include <functional>
#include <EGL/egl.h>

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
// SwapChainHook draws ImGui through host presentation hooks.
//
// Presentation path:
// Intercept eglSwapBuffers or vkQueuePresentKHR
// and draw into the host framebuffer before presentation.
//
// Host-resource reuse:
// No separate overlay SurfaceView or ANativeWindow is created by this path.
// The host EGL context or Vulkan device is reused.
// GPU rendering resources are still allocated for ImGui.
// This path does not create a separate WindowManager window.
// It does not create a separate SurfaceFlinger layer.
//
// Invocation from the initialization worker:
//   SwapChainHook::SetRenderCallback([]() {
//       UE_Hack::GetInstance().RenderMenu();
//       ...
//   });
//   SwapChainHook::Install();
// ---------------------------------------------------------------------------

namespace SwapChainHook
{

/// Install EGL and Vulkan presentation hooks.
/// The first intercepted graphics API initializes ImGui.
void Install();

/// Request hook removal and ImGui cleanup; EGL restoration is currently incomplete.
void Uninstall();

/// Whether ImGui initialization has completed after a hook callback.
bool IsInitialized();

/// Set the frame callback under a lock; callback reads are not consistently synchronized.
void SetRenderCallback(std::function<void()> callback);

/// Return detected host dimensions from EGLSurface or VkSwapchain.
int GetWidth();
int GetHeight();

} // namespace SwapChainHook
