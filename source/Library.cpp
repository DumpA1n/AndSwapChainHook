#include <jni.h>

// Loading establishes JNI version compatibility only. Components are initialized
// and started by the host through their explicit APIs.
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *, void *)
{
    return JNI_VERSION_1_6;
}
