#include "AndroidPlatform.h"
#include <jni.h>
#include "Core/ElfScannerManager.h"
#include "Utils/Logger.h"

struct android_app *g_App = nullptr;

JavaVM* g_JavaVM = nullptr; /// deprecated, use AndroidPlatform::GetJavaVM() instead

namespace AndroidPlatform {

/**
 * 通过 ElfScanner 解析 libart.so 的 JNI_GetCreatedJavaVMs 符号获取 JavaVM 指针
 */
JavaVM* GetJavaVM()
{
    static JavaVM* s_vm = nullptr;
    if (s_vm) return s_vm;

    auto addr = Elf.art().findSymbol("JNI_GetCreatedJavaVMs");
    if (!addr)
    {
        LOGE("[AndroidPlatform] GetJavaVM: findSymbol(JNI_GetCreatedJavaVMs) failed");
        return nullptr;
    }

    using JNI_GetCreatedJavaVMs_t = jint (*)(JavaVM**, jsize, jsize*);
    auto fn = reinterpret_cast<JNI_GetCreatedJavaVMs_t>(addr);

    JavaVM* vm = nullptr;
    jsize count = 0;
    if (fn(&vm, 1, &count) == JNI_OK && count > 0)
    {
        s_vm = vm;
        LOGI("[AndroidPlatform] GetJavaVM: got VM=%p", s_vm);
        return s_vm;
    }

    LOGE("[AndroidPlatform] GetJavaVM: JNI_GetCreatedJavaVMs failed, count=%d", (int)count);
    return nullptr;
}

JNIEnv *GetJavaEnv()
{
    JavaVM* vm = GetJavaVM();
    if (!vm)
    {
        LOGE("[AndroidPlatform] GetJavaEnv: JavaVM is null");
        return nullptr;
    }
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    LOGW("[AndroidPlatform] GetJavaEnv: GetEnv failed (thread not attached?)");
    return nullptr;
}

/**
 * 通过 JNI 反射获取 android_app*，适用于所有使用 NativeActivity + android_native_app_glue 的 Android 应用
 *
 * 原理：
 *   ActivityThread.mActivities -> ActivityClientRecord.activity -> NativeActivity
 *   NativeActivity.mNativeHandle (long) 即 ANativeActivity*
 *   ANativeActivity::instance 由 glue code 设置为 android_app*
 */
android_app* FindAndroidAppViaJNI()
{
    JavaVM* vm = GetJavaVM();
    if (!vm)
    {
        LOGE("[AndroidPlatform] FindAndroidAppViaJNI: JavaVM is null");
        return nullptr;
    }

    JNIEnv* env = nullptr;
    bool needDetach = false;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        {
            LOGE("[AndroidPlatform] FindAndroidAppViaJNI: AttachCurrentThread failed");
            return nullptr;
        }
        needDetach = true;
    }

    android_app* result = nullptr;

    // PushLocalFrame 确保所有 JNI 局部引用在 PopLocalFrame 时自动释放
    if (env->PushLocalFrame(32) == 0)
    {
        do {
            // 1. ActivityThread.currentActivityThread()
            jclass atClass = env->FindClass("android/app/ActivityThread");
            if (!atClass || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jmethodID catMethod = env->GetStaticMethodID(atClass, "currentActivityThread",
                "()Landroid/app/ActivityThread;");
            if (!catMethod || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jobject at = env->CallStaticObjectMethod(atClass, catMethod);
            if (!at || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            // 2. mActivities: ArrayMap<IBinder, ActivityClientRecord> (API 21+)
            jfieldID activitiesField = env->GetFieldID(atClass, "mActivities",
                "Landroid/util/ArrayMap;");
            if (!activitiesField || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jobject activities = env->GetObjectField(at, activitiesField);
            if (!activities || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            // 3. 遍历 ArrayMap 获取 ActivityClientRecord
            jclass mapClass = env->GetObjectClass(activities);
            jmethodID sizeMethod = env->GetMethodID(mapClass, "size", "()I");
            jmethodID valueAtMethod = env->GetMethodID(mapClass, "valueAt",
                "(I)Ljava/lang/Object;");
            if (!sizeMethod || !valueAtMethod) break;

            jint size = env->CallIntMethod(activities, sizeMethod);

            // 4. 查找 NativeActivity（含子类）并读取 mNativeHandle
            jclass naClass = env->FindClass("android/app/NativeActivity");
            if (!naClass || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jfieldID handleField = env->GetFieldID(naClass, "mNativeHandle", "J");
            if (!handleField || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            for (jint i = 0; i < size; i++)
            {
                jobject record = env->CallObjectMethod(activities, valueAtMethod, i);
                if (!record) continue;

                jclass recClass = env->GetObjectClass(record);
                jfieldID actField = env->GetFieldID(recClass, "activity",
                    "Landroid/app/Activity;");
                if (!actField || env->ExceptionCheck())
                {
                    env->ExceptionClear();
                    continue;
                }

                jobject activity = env->GetObjectField(record, actField);
                if (!activity) continue;

                // IsInstanceOf 会匹配 NativeActivity 及其所有子类（如 UE4 GameActivity）
                if (env->IsInstanceOf(activity, naClass))
                {
                    jlong handle = env->GetLongField(activity, handleField);
                    LOGI("[AndroidPlatform] FindAndroidAppViaJNI: NativeActivity found, mNativeHandle=0x%llx", (unsigned long long)handle);
                    if (handle != 0)
                    {
                        auto* na = reinterpret_cast<ANativeActivity*>(handle);
                        result = static_cast<android_app*>(na->instance);
                        LOGI("[AndroidPlatform] FindAndroidAppViaJNI: android_app=%p", result);
                    }
                }
                if (result) break;
            }
        } while (false);

        env->PopLocalFrame(nullptr);
    }

    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (needDetach)
        vm->DetachCurrentThread();

    return result;
}

/**
 * 通过 JNI 反射从当前 Activity 的 Window → DecorView → ViewRootImpl → Surface 获取 ANativeWindow*
 *
 * 原理：
 *   ActivityThread.currentActivityThread().mActivities → ActivityClientRecord.activity
 *   Activity.getWindow().getDecorView().getViewRootImpl().mSurface
 *   → ANativeWindow_fromSurface(env, surface)
 *
 * 适用于所有 Activity（包括 Godot/Unity 等不使用 NativeActivity 的引擎）
 */
ANativeWindow* FindNativeWindowViaJNI()
{
    JavaVM* vm = GetJavaVM();
    if (!vm)
    {
        LOGE("[AndroidPlatform] FindNativeWindowViaJNI: JavaVM is null");
        return nullptr;
    }

    JNIEnv* env = nullptr;
    bool needDetach = false;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        {
            LOGE("[AndroidPlatform] FindNativeWindowViaJNI: AttachCurrentThread failed");
            return nullptr;
        }
        needDetach = true;
    }

    ANativeWindow* result = nullptr;

    if (env->PushLocalFrame(32) == 0)
    {
        do {
            // 1. ActivityThread.currentActivityThread()
            jclass atClass = env->FindClass("android/app/ActivityThread");
            if (!atClass || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jmethodID catMethod = env->GetStaticMethodID(atClass, "currentActivityThread",
                "()Landroid/app/ActivityThread;");
            if (!catMethod || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jobject at = env->CallStaticObjectMethod(atClass, catMethod);
            if (!at || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            // 2. mActivities: ArrayMap<IBinder, ActivityClientRecord>
            jfieldID activitiesField = env->GetFieldID(atClass, "mActivities",
                "Landroid/util/ArrayMap;");
            if (!activitiesField || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jobject activities = env->GetObjectField(at, activitiesField);
            if (!activities || env->ExceptionCheck()) { env->ExceptionClear(); break; }

            // 3. 遍历 ArrayMap
            jclass mapClass = env->GetObjectClass(activities);
            jmethodID sizeMethod = env->GetMethodID(mapClass, "size", "()I");
            jmethodID valueAtMethod = env->GetMethodID(mapClass, "valueAt",
                "(I)Ljava/lang/Object;");
            if (!sizeMethod || !valueAtMethod) break;

            jint size = env->CallIntMethod(activities, sizeMethod);

            for (jint i = 0; i < size; i++)
            {
                jobject record = env->CallObjectMethod(activities, valueAtMethod, i);
                if (!record) continue;

                // record.activity
                jclass recClass = env->GetObjectClass(record);
                jfieldID actField = env->GetFieldID(recClass, "activity",
                    "Landroid/app/Activity;");
                if (!actField || env->ExceptionCheck())
                {
                    env->ExceptionClear();
                    continue;
                }

                jobject activity = env->GetObjectField(record, actField);
                if (!activity) continue;

                // Activity.getWindow()
                jclass actClass = env->FindClass("android/app/Activity");
                if (!actClass || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jmethodID getWindowMethod = env->GetMethodID(actClass, "getWindow",
                    "()Landroid/view/Window;");
                if (!getWindowMethod || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jobject window = env->CallObjectMethod(activity, getWindowMethod);
                if (!window || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                // Window.getDecorView()
                jclass windowClass = env->GetObjectClass(window);
                jmethodID getDecorViewMethod = env->GetMethodID(windowClass, "getDecorView",
                    "()Landroid/view/View;");
                if (!getDecorViewMethod || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jobject decorView = env->CallObjectMethod(window, getDecorViewMethod);
                if (!decorView || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                // View.getViewRootImpl()（隐藏 API，所有 Android 版本均存在）
                jclass viewClass = env->FindClass("android/view/View");
                if (!viewClass || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jmethodID getVRIMethod = env->GetMethodID(viewClass, "getViewRootImpl",
                    "()Landroid/view/ViewRootImpl;");
                if (!getVRIMethod || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jobject vri = env->CallObjectMethod(decorView, getVRIMethod);
                if (!vri || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                // ViewRootImpl.mSurface
                jclass vriClass = env->GetObjectClass(vri);
                jfieldID surfaceField = env->GetFieldID(vriClass, "mSurface",
                    "Landroid/view/Surface;");
                if (!surfaceField || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jobject surface = env->GetObjectField(vri, surfaceField);
                if (!surface || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                // Surface.isValid() 检查
                jclass surfClass = env->GetObjectClass(surface);
                jmethodID isValidMethod = env->GetMethodID(surfClass, "isValid", "()Z");
                if (isValidMethod && !env->ExceptionCheck())
                {
                    jboolean valid = env->CallBooleanMethod(surface, isValidMethod);
                    if (!valid)
                    {
                        LOGW("[AndroidPlatform] FindNativeWindowViaJNI: Surface not valid for activity[%d]", (int)i);
                        continue;
                    }
                }
                if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                // ANativeWindow_fromSurface — 返回的 window 已被 acquire
                result = ANativeWindow_fromSurface(env, surface);
                if (result)
                {
                    LOGI("[AndroidPlatform] FindNativeWindowViaJNI: ANativeWindow=%p (%dx%d)",
                         result, ANativeWindow_getWidth(result), ANativeWindow_getHeight(result));
                }
                if (result) break;
            }
        } while (false);

        env->PopLocalFrame(nullptr);
    }

    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (needDetach)
        vm->DetachCurrentThread();

    return result;
}

ANativeWindow* GetNativeWindow()
{
    if (g_App && g_App->window)
        return g_App->window;

    static ANativeWindow* s_cachedWindow = nullptr;
    if (s_cachedWindow)
        return s_cachedWindow;

    s_cachedWindow = FindNativeWindowViaJNI();
    return s_cachedWindow;
}

}
