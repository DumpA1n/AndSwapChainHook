#include <AndSwapChainHook/Overlay.h>
#include <android/native_window.h>
#include <jni.h>
#include <thread>
using namespace AndSwapChainHook::Android;
namespace
{
SurfaceView *surface = nullptr;
std::uint64_t previousGeneration = 0;
} // namespace
extern "C" JNIEXPORT jint JNICALL Java_com_andswapchain_owned_MainActivity_create(JNIEnv *env,
                                                                                  jclass,
                                                                                  jobject activity)
{
    return static_cast<jint>(SurfaceView::Create(env, activity, 160, 160, surface));
}
extern "C" JNIEXPORT jint JNICALL Java_com_andswapchain_owned_MainActivity_poll(JNIEnv *env, jclass)
{
    return surface ? static_cast<jint>(surface->Poll(env)) : -1;
}
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
            return __LINE__;                                                                       \
    } while (false)
extern "C" JNIEXPORT jint JNICALL
Java_com_andswapchain_owned_MainActivity_exerciseAndDestroy(JNIEnv *env, jclass)
{
    CHECK(surface);
    auto stats = surface->Stats();
    CHECK(stats.generation > previousGeneration && stats.globalRefs == 3 &&
          stats.nativeWindowRefs == 1 && stats.attachedViews == 1);
    previousGeneration = stats.generation;
    CHECK(!surface->AcquireLease(stats.generation + 1));
    auto lease = surface->AcquireLease(stats.generation);
    CHECK(lease && lease->Window() && lease->Generation() == stats.generation &&
          surface->Stats().leases == 1);
    CHECK(surface->Retire(env) == SurfaceError::Busy);
    CHECK(!surface->AcquireLease(stats.generation));
    CHECK(SurfaceView::Destroy(env, surface) == SurfaceError::Busy && surface);
    bool rendered = false;
    std::thread render(
        [owned = std::move(*lease), &rendered]() mutable
        {
            ANativeWindow_Buffer buffer{};
            if (ANativeWindow_lock(owned.Window(), &buffer, nullptr) == 0)
            {
                const bool dimensions = buffer.bits && buffer.width > 0 && buffer.height > 0 &&
                                        buffer.stride >= buffer.width;
                const bool rgba = buffer.format == WINDOW_FORMAT_RGBA_8888 ||
                                  buffer.format == WINDOW_FORMAT_RGBX_8888;
                const bool rgb565 = buffer.format == WINDOW_FORMAT_RGB_565;
                if (dimensions && (rgba || rgb565))
                {
                    for (int y = 0; y < buffer.height; ++y)
                        for (int x = 0; x < buffer.width; ++x)
                        {
                            const auto index = static_cast<std::size_t>(y) * buffer.stride + x;
                            if (rgba)
                                static_cast<std::uint32_t *>(buffer.bits)[index] = 0xff3366aa;
                            else
                                static_cast<std::uint16_t *>(buffer.bits)[index] = 0x3335;
                        }
                }
                rendered = ANativeWindow_unlockAndPost(owned.Window()) == 0 && dimensions &&
                           (rgba || rgb565);
            }
        });
    render.join();
    lease.reset();
    CHECK(rendered && surface->Stats().leases == 0);
    CHECK(surface->Retire(env) == SurfaceError::None);
    stats = surface->Stats();
    CHECK(stats.globalRefs == 0 && stats.nativeWindowRefs == 0 && stats.attachedViews == 0 &&
          stats.leases == 0);
    CHECK(SurfaceView::Destroy(env, surface) == SurfaceError::None && !surface);
    return 0;
}
extern "C" JNIEXPORT void JNICALL Java_com_andswapchain_owned_MainActivity_cleanup(JNIEnv *env,
                                                                                   jclass)
{
    SurfaceView::Destroy(env, surface);
}
