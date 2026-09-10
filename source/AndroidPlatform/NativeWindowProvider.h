#pragma once

#include <android/native_window.h>

// ---------------------------------------------------------------------------
// NativeWindowProvider: host ANativeWindow access.
//
// Resolve the Activity through ActivityHost, then follow
//   Activity.getWindow().getDecorView().getViewRootImpl().mSurface
// → ANativeWindow_fromSurface
// to obtain ANativeWindow; cache it for process lifetime.
// ---------------------------------------------------------------------------
namespace AndroidPlatform
{

// Return the host's main ANativeWindow, using the cache when populated.
// Return nullptr when the Activity or a valid Surface is unavailable.
ANativeWindow* GetNativeWindow();

}
