#pragma once

#include <android/input.h>
#include <functional>

using InputEventCallback_t = std::function<void(AInputEvent* event)>;

namespace InputEventHook
{
    void Initialize(InputEventCallback_t callback);

    void SetInputEventCallback(InputEventCallback_t callback);
}
