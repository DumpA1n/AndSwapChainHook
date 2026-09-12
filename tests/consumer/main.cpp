#include <AndSwapChainHook/CallbackGate.h>
#include <AndSwapChainHook/HookTransaction.h>
#include <AndSwapChainHook/Input.h>
#include <AndSwapChainHook/Memory.h>
#include <AndSwapChainHook/Runtime.h>
#if defined(CONSUME_ANDROID_MODULES)
#include <AndSwapChainHook/CrashRecorder.h>
#include <AndSwapChainHook/Graphics.h>
#include <AndSwapChainHook/Overlay.h>
#endif
int main()
{
    ASC_Session *session = nullptr;
    if (ASC_Create(&session).error != ASC_OK ||
        ASC_Snapshot(session, nullptr, 0).state != ASC_CREATED)
        return 4;
    ASC_Destroy(session);
#if defined(CONSUME_ANDROID_MODULES)
    if (AndSwapChainHook::CrashRecorder::DroppedRecords() != 0)
        return 5;
    AndSwapChainHook::Android::SurfaceView *surface = nullptr;
    if (AndSwapChainHook::Android::SurfaceView::Destroy(nullptr, surface) !=
        AndSwapChainHook::Android::SurfaceError::None)
        return 2;
    if (SwapChainHook::Install().error !=
        AndSwapChainHook::Instrumentation::HookError::QuiescenceRequired)
        return 3;
#endif
    AndSwapChainHook::Memory::ModuleCatalog catalog;
    return catalog.Replace({}) == AndSwapChainHook::Memory::Error::None ? 0 : 1;
}
