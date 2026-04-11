#pragma once

#include <functional>

namespace SwapChainHook
{

void Install();

void Uninstall();

bool IsInitialized();

void SetRenderCallback(std::function<void()> callback);

int GetWidth();

int GetHeight();

} // namespace SwapChainHook
