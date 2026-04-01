#pragma once

// Android 平台基础：全局 android_app / JavaVM / JNI 环境获取
// 整个项目通过此头文件访问平台上下文

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "android_native_app_glue.h"
#include <jni.h>

inline struct android_app *g_App = nullptr;

inline JavaVM *g_JavaVM = nullptr;

inline JNIEnv *GetJavaEnv()
{
    JNIEnv *env = nullptr;
    if (g_JavaVM->GetEnv((void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
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
inline android_app* FindAndroidAppViaJNI(JavaVM* vm)
{
    JNIEnv* env = nullptr;
    bool needDetach = false;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
            return nullptr;
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
                    if (handle != 0)
                    {
                        auto* na = reinterpret_cast<ANativeActivity*>(handle);
                        result = static_cast<android_app*>(na->instance);
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
