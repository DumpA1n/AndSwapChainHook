#include <chrono>
#include <cstdint>
#include <thread>

#include "AndroidPlatform/AndroidPlatform.h"
#include "AndroidPlatform/LooperDispatcher.h"
#include "ImGui/ImGuiHost.h"
#include "InputEvent/InputEventHook.h"
#include "Utils/Config/Config.h"
#include "Utils/CrashHandler/CrashHandler.h"
#include "Utils/ElfScanner/ElfScannerManager.h"
#include "Utils/HookUtils.h"
#include "Utils/KittyEx.h"
#include "Utils/Logger.h"

#include "imgui/backends/imgui_impl_android.h"

static LooperDispatcher g_MainLooperDispatcher;

void main_thread()
{
	KT::Init();

	if (!Elf.Scan({



			"libinput.so", // For InputEventHook
			"libart.so", // For GetJavaVM()

		}))
	{
		LOGE("Failed to scan necessary libraries.");
		MAKE_CRASH();
	}

	GetLogFile("Debug")->Append("Hello\n");

	Config::AutoLoadOnStartup();

	ImGuiHost::Init({
		.mode         = (CFG.InjectionMode == 0) ? InjectionMode::SwapHook : InjectionMode::Overlay,
		.preferredApi = static_cast<GraphicsAPI>(CFG.RenderBackend),
		.render = []()
		{
			ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("AndSwapChainHook"))
			{
				ImGui::SeparatorText("Hook Mode");
				const char* kModes[] = { "SwapHook", "Overlay" };
				if (ImGui::Combo("##mode", &CFG.InjectionMode, kModes, IM_ARRAYSIZE(kModes)))
					Config::SaveToActive();
				ImGui::TextDisabled("Applies on next launch");

				ImGui::SeparatorText("Render Backend");
				const char* kApis[] = { "OpenGL", "Vulkan" };
				if (ImGui::Combo("##api", &CFG.RenderBackend, kApis, IM_ARRAYSIZE(kApis)))
				{
					ImGuiHost::RequestSwitchBackend(static_cast<GraphicsAPI>(CFG.RenderBackend));
					Config::SaveToActive();
				}
				if (ImGuiHost::GetMode() == InjectionMode::SwapHook)
					ImGui::TextDisabled("SwapHook follows the host app");

				ImGui::SeparatorText("Config");
				if (ImGui::Checkbox("Auto load on startup", &CFG.bAutoLoadConfigOnStartup))
					Config::SaveToActive();
				if (ImGui::Button("Save"))   Config::SaveToActive();
				ImGui::SameLine();
				if (ImGui::Button("Reload")) Config::LoadFromActive();
				ImGui::SameLine();
				ImGui::Text("[%s]", Config::GetActiveName().c_str());

				ImGui::SeparatorText("Debug");
				static bool s_ShowDemo = false;
				ImGui::Checkbox("ImGui Demo Window", &s_ShowDemo);
				if (s_ShowDemo) ImGui::ShowDemoWindow(&s_ShowDemo);
			}
			ImGui::End();
		},
		.postToMainThread = [](std::function<void()> task)
		{
			g_MainLooperDispatcher.post(std::move(task));
		},
	});
	g_MainLooperDispatcher.post([]() { g_MainLooperDispatcher.cleanup(); });

	InputEventHook::Initialize([](AInputEvent* event)
	{
		if (!event) return;

		if (ImGuiHost::IsInitialized())
		{
			ImGui_ImplAndroid_HandleInputEvent(event);
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

	CrashHandler::Install();




}

__attribute__((destructor)) void dtor() { LOGI("dtor"); }

#include "AndroidPlatform/android_native_app_glue.h"
extern "C" void android_main(struct android_app* /*state*/) { app_dummy(); }
