#pragma once

// ---------------------------------------------------------------------------
// AndroidPlatform: host platform access headers.
//
//   - JniRuntime.h          : GetJavaVM / GetJavaEnv
//   - ActivityHost.h        : GetActivity （Java Activity GlobalRef）
//   - NativeWindowProvider.h: GetNativeWindow （ANativeWindow*）
// DisplayInfo.h: GetDisplaySize in physical pixels.
// ---------------------------------------------------------------------------

#include "JniRuntime.h"
#include "ActivityHost.h"
#include "NativeWindowProvider.h"
#include "DisplayInfo.h"
