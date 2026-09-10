#pragma once

#include <jni.h>

// ---------------------------------------------------------------------------
// JniRuntime: JavaVM and JNIEnv access.
//
// Resolve JNI_GetCreatedJavaVMs from libart.so rather than depending on
// a JavaVM supplied to JNI_OnLoad, which may be unavailable during early loading.
// GetJavaEnv does not attach threads; callers performing cross-thread JNI must
// AttachCurrentThread/DetachCurrentThread。
// ---------------------------------------------------------------------------
namespace AndroidPlatform
{

JavaVM* GetJavaVM();

// Return the current thread's JNIEnv, or nullptr if it is not attached.
JNIEnv* GetJavaEnv();

}
