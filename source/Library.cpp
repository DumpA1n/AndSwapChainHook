#include "Core/ElfScannerManager.h"
#include "Platform/AndroidApp.h"
#include "SwapChain/SwapChainHook.h"
#include "Utils/CrashHandler.h"
#include "Utils/FileLogger.h"
#include "Utils/HookUtils.h"
#include "Utils/Logger.h"
#include "imgui/imgui.h"

#include <chrono>
#include <thread>

void main_thread()
{
	if (!Elf.scanAsync({
			"libc.so",
			// "libUE4.so", // Enable when injecting Unreal Engine games and use VkGIPA_Pointer hook strategy
			"libvulkan.so",
			"libinput.so",
			"libandroid_runtime.so",
		}))
	{
		LOGE("Failed to scan necessary libraries.");
		MAKE_CRASH();
	}

	while (!g_App)
	{
		g_App = FindAndroidAppViaJNI(g_JavaVM);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	LOGI("[*] g_App: %p", g_App);

	GetLogFile("Debug")->Append("Hello\n"); // Must after g_App is valid

	SwapChainHook::SetRenderCallback([]()
	{
		ImGui::ShowDemoWindow();
	});
	SwapChainHook::Install();
}

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key)
{
	// key 1337 is passed by injector
	if (key != (void*)1337)
		return JNI_VERSION_1_6;

	CrashHandler::Install();

	LOGI("JNI_OnLoad called by injector.");

	LOGI("JavaVM: %p", vm);

	g_JavaVM = vm;

	JNIEnv* env = nullptr;
	if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK)
	{
		LOGI("JavaEnv: %p", env);
	}

	std::thread(main_thread).detach();

	return JNI_VERSION_1_6;
}

__attribute__((constructor)) void ctor() { LOGI("ctor"); }

__attribute__((destructor)) void dtor() { LOGI("dtor"); }
