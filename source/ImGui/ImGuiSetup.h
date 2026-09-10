#pragma once

struct ANativeWindow;

// ---------------------------------------------------------------------------
// ImGuiSetup: platform-independent context setup.
//
// Creates the context, applies style and initializes the Android backend.
//
// The caller remains responsible for:
//   - ImGui_ImplOpenGL3_Init / ImGui_ImplVulkan_Init
// ResourceManager::initializeFonts after renderer initialization.
// io.DisplaySize, updated by ImGui_ImplAndroid_NewFrame each frame.
// If initialization needs an explicit size, such as for Vulkan preTransform,
// assign io.DisplaySize after EnsureContext.
// ---------------------------------------------------------------------------
namespace ImGuiSetup
{
    // Idempotent when initialized; platformWindow is passed to ImGui_ImplAndroid_Init.
    void EnsureContext(ANativeWindow *platformWindow);

    // ImGui_ImplAndroid_Shutdown + DestroyContext。
    // Shut down the graphics backend before calling this cleanup.
    void TeardownContext();
}
