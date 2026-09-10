#pragma once

#include <jni.h>

// ---------------------------------------------------------------------------
// ActivityHost: host Activity access.
//
// Resolve an Activity through ActivityThread.mActivities.
// Any Activity subclass is accepted.
// Cache a GlobalRef for process lifetime.
// ---------------------------------------------------------------------------
namespace AndroidPlatform
{

// Return a GlobalRef to a host Activity.
// Return the cached reference in O(1) when available.
// Otherwise resolve through reflection; return nullptr when unavailable.
// Temporarily attach the calling thread if needed; the GlobalRef is cross-thread.
jobject GetActivity();

}
