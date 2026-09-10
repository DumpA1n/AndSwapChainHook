#pragma once

#include "imgui/imgui.h"

// ---------------------------------------------------------------------------
// ImGuiSoftKeyboard: ImGui software keyboard.
//
// When io.WantTextInput is set, render a
// QWERTY keyboard and deliver characters with io.AddInputCharacter().
// This keyboard does not use the Android IME framework.
//
// Usage:
// Call each frame before ImGui::Render().
//   ImGuiSoftKeyboard::Draw();
// ---------------------------------------------------------------------------

namespace ImGuiSoftKeyboard
{

/// Call after NewFrame and before user UI to intercept pointer events over the keyboard.
void PreUpdate();

/// Call after user UI and before Render to draw the keyboard and process keys.
void Draw();

/// Override automatic visibility.
void ForceShow(bool show);

/// Whether the keyboard is visible.
bool IsVisible();

} // namespace ImGuiSoftKeyboard
