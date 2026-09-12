#include <AndSwapChainHook/Overlay.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <atomic>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace AndSwapChainHook::Android
{
struct SurfaceState
{
    mutable std::mutex mutex;
    std::thread::id owner;
    std::uint64_t generation = 0;
    std::uint32_t leases = 0;
    jobject activity = nullptr, manager = nullptr, view = nullptr;
    ANativeWindow *window = nullptr;
    bool attached = false, retiring = false;
};
namespace
{
std::atomic<std::uint64_t> nextGeneration{1};
bool ClearException(JNIEnv *env)
{
    if (!env->ExceptionCheck())
        return false;
    env->ExceptionClear();
    return true;
}
class LocalFrame
{
  public:
    explicit LocalFrame(JNIEnv *env) : env_(env), valid_(env && env->PushLocalFrame(32) == JNI_OK)
    {
    }
    ~LocalFrame()
    {
        if (valid_)
            env_->PopLocalFrame(nullptr);
    }
    explicit operator bool() const
    {
        return valid_;
    }

  private:
    JNIEnv *env_;
    bool valid_;
};
bool MainThread(JNIEnv *env)
{
    LocalFrame frame(env);
    if (!frame)
        return false;
    jclass looper = env->FindClass("android/os/Looper");
    if (ClearException(env) || !looper)
        return false;
    jmethodID current = env->GetStaticMethodID(looper, "myLooper", "()Landroid/os/Looper;");
    if (ClearException(env) || !current)
        return false;
    jmethodID main = env->GetStaticMethodID(looper, "getMainLooper", "()Landroid/os/Looper;");
    if (ClearException(env) || !main)
        return false;
    jobject currentLooper = env->CallStaticObjectMethod(looper, current);
    if (ClearException(env))
        return false;
    jobject mainLooper = env->CallStaticObjectMethod(looper, main);
    return !ClearException(env) && currentLooper && mainLooper &&
           env->IsSameObject(currentLooper, mainLooper);
}
SurfaceError CheckOwner(JNIEnv *env, const SurfaceState &state)
{
    if (!env)
        return SurfaceError::InvalidArgument;
    return state.owner == std::this_thread::get_id() ? SurfaceError::None
                                                     : SurfaceError::WrongThread;
}
void Release(JNIEnv *env, SurfaceState &state)
{
    if (state.window)
    {
        ANativeWindow_release(state.window);
        state.window = nullptr;
    }
    for (auto *ref : {&state.activity, &state.manager, &state.view})
        if (*ref)
        {
            env->DeleteGlobalRef(*ref);
            *ref = nullptr;
        }
}
jobject LayoutParams(JNIEnv *env, int width, int height)
{
    jclass cls = env->FindClass("android/view/WindowManager$LayoutParams");
    if (ClearException(env) || !cls)
        return nullptr;
    jmethodID constructor = env->GetMethodID(cls, "<init>", "(IIIII)V");
    if (ClearException(env) || !constructor)
        return nullptr;
    // TYPE_APPLICATION, FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCH_MODAL, RGBA_8888.
    jobject params = env->NewObject(cls, constructor, width, height, 2, 8 | 32, 1);
    return ClearException(env) ? nullptr : params;
}
} // namespace
SurfaceLease::SurfaceLease(std::shared_ptr<SurfaceState> state) : state_(std::move(state)) {}
SurfaceLease::~SurfaceLease()
{
    Reset();
}
SurfaceLease::SurfaceLease(SurfaceLease &&other) noexcept : state_(std::move(other.state_)) {}
SurfaceLease &SurfaceLease::operator=(SurfaceLease &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        state_ = std::move(other.state_);
    }
    return *this;
}
void SurfaceLease::Reset() noexcept
{
    auto state = std::move(state_);
    if (state)
    {
        std::lock_guard lock(state->mutex);
        --state->leases;
    }
}
ANativeWindow *SurfaceLease::Window() const noexcept
{
    return state_ ? state_->window : nullptr;
}
std::uint64_t SurfaceLease::Generation() const noexcept
{
    return state_ ? state_->generation : 0;
}
SurfaceView::SurfaceView(std::shared_ptr<SurfaceState> state) : state_(std::move(state)) {}
SurfaceError SurfaceView::Create(JNIEnv *env, jobject activity, int width, int height,
                                 SurfaceView *&output)
{
    if (output || !env || !activity || width <= 0 || height <= 0)
        return SurfaceError::InvalidArgument;
    if (!MainThread(env))
        return SurfaceError::WrongThread;
    LocalFrame frame(env);
    if (!frame)
        return SurfaceError::JniError;
    std::shared_ptr<SurfaceState> state;
    try
    {
        state = std::make_shared<SurfaceState>();
    }
    catch (...)
    {
        return SurfaceError::CapacityExceeded;
    }
    state->owner = std::this_thread::get_id();
    auto generation = nextGeneration.load(std::memory_order_relaxed);
    do
    {
        if (generation == std::numeric_limits<std::uint64_t>::max())
            return SurfaceError::CapacityExceeded;
    } while (!nextGeneration.compare_exchange_weak(generation, generation + 1,
                                                   std::memory_order_relaxed));
    state->generation = generation;
    jclass activityClass = env->GetObjectClass(activity);
    if (ClearException(env) || !activityClass)
        return SurfaceError::JniError;
    jmethodID service = env->GetMethodID(activityClass, "getSystemService",
                                         "(Ljava/lang/String;)Ljava/lang/Object;");
    if (ClearException(env) || !service)
        return SurfaceError::JniError;
    jstring serviceName = env->NewStringUTF("window");
    if (ClearException(env) || !serviceName)
        return SurfaceError::JniError;
    jobject manager = env->CallObjectMethod(activity, service, serviceName);
    if (ClearException(env) || !manager)
        return SurfaceError::JniError;
    jclass managerClass = env->GetObjectClass(manager);
    if (ClearException(env) || !managerClass)
        return SurfaceError::JniError;
    jmethodID add = env->GetMethodID(managerClass, "addView",
                                     "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    if (ClearException(env) || !add)
        return SurfaceError::JniError;
    jclass viewClass = env->FindClass("android/view/SurfaceView");
    if (ClearException(env) || !viewClass)
        return SurfaceError::JniError;
    jmethodID constructor = env->GetMethodID(viewClass, "<init>", "(Landroid/content/Context;)V");
    if (ClearException(env) || !constructor)
        return SurfaceError::JniError;
    jobject view = env->NewObject(viewClass, constructor, activity);
    if (ClearException(env) || !view)
        return SurfaceError::JniError;
    jobject params = LayoutParams(env, width, height);
    if (!params)
        return SurfaceError::JniError;
    state->activity = env->NewGlobalRef(activity);
    if (ClearException(env) || !state->activity)
    {
        Release(env, *state);
        return SurfaceError::JniError;
    }
    state->manager = env->NewGlobalRef(manager);
    if (ClearException(env) || !state->manager)
    {
        Release(env, *state);
        return SurfaceError::JniError;
    }
    state->view = env->NewGlobalRef(view);
    if (ClearException(env) || !state->view)
    {
        Release(env, *state);
        return SurfaceError::JniError;
    }
    // Allocate the owner before attaching a framework resource.
    SurfaceView *owner = nullptr;
    try
    {
        owner = new SurfaceView(state);
    }
    catch (...)
    {
        Release(env, *state);
        return SurfaceError::CapacityExceeded;
    }
    env->CallVoidMethod(manager, add, view, params);
    if (ClearException(env))
    {
        Release(env, *state);
        delete owner;
        return SurfaceError::JniError;
    }
    state->attached = true;
    output = owner;
    return SurfaceError::None;
}
SurfaceError SurfaceView::Poll(JNIEnv *env)
{
    auto &state = *state_;
    if (const auto error = CheckOwner(env, state); error != SurfaceError::None)
        return error;
    std::lock_guard lock(state.mutex);
    if (state.retiring)
        return SurfaceError::Retiring;
    if (state.window)
        return SurfaceError::None;
    LocalFrame frame(env);
    if (!frame)
        return SurfaceError::JniError;
    jclass viewClass = env->GetObjectClass(state.view);
    if (ClearException(env) || !viewClass)
        return SurfaceError::JniError;
    jmethodID getHolder =
        env->GetMethodID(viewClass, "getHolder", "()Landroid/view/SurfaceHolder;");
    if (ClearException(env) || !getHolder)
        return SurfaceError::JniError;
    jobject holder = env->CallObjectMethod(state.view, getHolder);
    if (ClearException(env) || !holder)
        return SurfaceError::JniError;
    jclass holderClass = env->GetObjectClass(holder);
    if (ClearException(env) || !holderClass)
        return SurfaceError::JniError;
    jmethodID getSurface = env->GetMethodID(holderClass, "getSurface", "()Landroid/view/Surface;");
    if (ClearException(env) || !getSurface)
        return SurfaceError::JniError;
    jobject surface = env->CallObjectMethod(holder, getSurface);
    if (ClearException(env) || !surface)
        return SurfaceError::JniError;
    jclass surfaceClass = env->GetObjectClass(surface);
    if (ClearException(env) || !surfaceClass)
        return SurfaceError::JniError;
    jmethodID isValid = env->GetMethodID(surfaceClass, "isValid", "()Z");
    if (ClearException(env) || !isValid)
        return SurfaceError::JniError;
    const bool valid = env->CallBooleanMethod(surface, isValid);
    if (ClearException(env))
        return SurfaceError::JniError;
    if (!valid)
        return SurfaceError::NotReady;
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (ClearException(env))
    {
        if (window)
            ANativeWindow_release(window);
        return SurfaceError::JniError;
    }
    if (!window)
        return SurfaceError::NotReady;
    if (ANativeWindow_getWidth(window) <= 0 || ANativeWindow_getHeight(window) <= 0)
    {
        ANativeWindow_release(window);
        return SurfaceError::NotReady;
    }
    state.window = window;
    return SurfaceError::None;
}
std::optional<SurfaceLease> SurfaceView::AcquireLease(std::uint64_t generation)
{
    std::lock_guard lock(state_->mutex);
    if (state_->retiring || !state_->window || generation != state_->generation ||
        state_->leases == std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    ++state_->leases;
    return SurfaceLease(state_);
}
SurfaceError SurfaceView::Retire(JNIEnv *env)
{
    auto &state = *state_;
    if (const auto error = CheckOwner(env, state); error != SurfaceError::None)
        return error;
    std::lock_guard lock(state.mutex);
    state.retiring = true;
    if (state.leases)
        return SurfaceError::Busy;
    if (state.attached)
    {
        LocalFrame frame(env);
        if (!frame)
            return SurfaceError::JniError;
        jclass cls = env->GetObjectClass(state.manager);
        if (ClearException(env) || !cls)
            return SurfaceError::JniError;
        jmethodID remove = env->GetMethodID(cls, "removeViewImmediate", "(Landroid/view/View;)V");
        if (ClearException(env) || !remove)
            return SurfaceError::JniError;
        env->CallVoidMethod(state.manager, remove, state.view);
        if (ClearException(env))
            return SurfaceError::JniError;
        state.attached = false;
    }
    Release(env, state);
    return SurfaceError::None;
}
SurfaceError SurfaceView::Destroy(JNIEnv *env, SurfaceView *&surface)
{
    if (!surface)
        return SurfaceError::None;
    const auto result = surface->Retire(env);
    if (result == SurfaceError::None)
    {
        delete surface;
        surface = nullptr;
    }
    return result;
}
SurfaceStats SurfaceView::Stats() const
{
    std::lock_guard lock(state_->mutex);
    return {state_->generation,
            state_->leases,
            static_cast<std::uint32_t>(!!state_->activity + !!state_->manager + !!state_->view),
            static_cast<std::uint32_t>(state_->window != nullptr),
            static_cast<std::uint32_t>(state_->attached),
            state_->retiring};
}
} // namespace AndSwapChainHook::Android
