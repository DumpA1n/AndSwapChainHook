#include "AndroidPlatform/AndroidPlatform.h"
#include "Core/ElfScannerManager.h"
#include "InputEvent/CustomHandleInput.h"
#include "InputEvent/InputEventHook.h"
#include "SwapChain/SwapChainHook.h"
#include "Utils/CrashHandler.h"
#include "Utils/FileLogger.h"
#include "Utils/HookUtils.h"
#include "Utils/Logger.h"
#include "imgui/imgui.h"

#include <atomic>
#include <chrono>
#include <thread>

void main_thread()
{
	CrashHandler::Install();

	KT::Init();

	if (!Elf.scanAsync({
			// "libc.so",
			// "libUE4.so", // For Unreal Engine 4
			// "libvulkan.so",
			"libinput.so", // For InputEventHook
			"libart.so", // For GetJavaVM()
			// "libandroid_runtime.so",
		}))
	{
		LOGE("Failed to scan necessary libraries.");
		MAKE_CRASH();
	}

	GetLogFile("Debug")->Append("Hello\n");

	SwapChainHook::SetRenderCallback([]() { ImGui::ShowDemoWindow(); });
	SwapChainHook::Install();

	InputEventHook::Initialize([](AInputEvent* event)
	{
        if (event)
        {
            if (SwapChainHook::IsInitialized())
            {
				const ImVec2 size = { (float)SwapChainHook::GetWidth(), (float)SwapChainHook::GetHeight() };
                CustomHandleInput::ImGui_ImplAndroid_HandleInputEvent(event, size);
            }

            int32_t event_type = AInputEvent_getType(event);
            if (event_type == AINPUT_EVENT_TYPE_KEY)
            {
                int32_t event_key_code = AKeyEvent_getKeyCode(event);
                int32_t event_action = AKeyEvent_getAction(event);
                if (event_key_code == AKEYCODE_VOLUME_DOWN && event_action == AKEY_EVENT_ACTION_DOWN)
                {
                    LOGI("keycode: AKEYCODE_VOLUME_DOWN, action: AKEY_EVENT_ACTION_DOWN");
                }
                else if (event_key_code == AKEYCODE_VOLUME_UP && event_action == AKEY_EVENT_ACTION_DOWN)
                {
                    LOGI("keycode: AKEYCODE_VOLUME_UP, action: AKEY_EVENT_ACTION_DOWN");
                }
            }
        }
    });
}

static std::atomic<bool> g_Initialized{false};

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key)
{
	// key 1337 is passed by injector
	if (key != (void*)1337)
		return JNI_VERSION_1_6;

	LOGI("JNI_OnLoad called by injector.");

	LOGI("JavaVM: %p", vm);

	JNIEnv* env = nullptr;
	if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK)
	{
		LOGI("JavaEnv: %p", env);
	}

	if (!g_Initialized.exchange(true))
		std::thread(main_thread).detach();

	return JNI_VERSION_1_6;
}

__attribute__((constructor)) void ctor()
{
	LOGI("ctor");

	// Enable if not use AndKittyInjector
	// if (!g_Initialized.exchange(true))
	// 	std::thread(main_thread).detach();
}

__attribute__((destructor)) void dtor() { LOGI("dtor"); }
