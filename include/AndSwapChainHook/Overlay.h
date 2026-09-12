#pragma once
#include <cstdint>
#include <jni.h>
#include <memory>
#include <optional>

struct ANativeWindow;
namespace AndSwapChainHook::Android
{
enum class SurfaceError
{
    None,
    InvalidArgument,
    WrongThread,
    NotReady,
    Busy,
    JniError,
    StaleGeneration,
    Retiring,
    CapacityExceeded
};
struct SurfaceStats
{
    std::uint64_t generation = 0;
    std::uint32_t leases = 0;
    std::uint32_t globalRefs = 0;
    std::uint32_t nativeWindowRefs = 0;
    std::uint32_t attachedViews = 0;
    bool retiring = false;
};
struct SurfaceState;
class SurfaceLease
{
  public:
    SurfaceLease() = default;
    ~SurfaceLease();
    SurfaceLease(SurfaceLease &&) noexcept;
    SurfaceLease &operator=(SurfaceLease &&) noexcept;
    SurfaceLease(const SurfaceLease &) = delete;
    SurfaceLease &operator=(const SurfaceLease &) = delete;
    ANativeWindow *Window() const noexcept;
    std::uint64_t Generation() const noexcept;
    void Reset() noexcept;

  private:
    friend class SurfaceView;
    explicit SurfaceLease(std::shared_ptr<SurfaceState> state);
    std::shared_ptr<SurfaceState> state_;
};
// The UI owner calls Create, Poll, Retire and Destroy with its current JNIEnv.
// The supplied Activity is authoritative; no Activity or window is discovered.
// Rendering threads borrow the acquired ANativeWindow through generation leases.
class SurfaceView
{
  public:
    static SurfaceError Create(JNIEnv *env, jobject activity, int width, int height,
                               SurfaceView *&output);
    SurfaceError Poll(JNIEnv *env);
    std::optional<SurfaceLease> AcquireLease(std::uint64_t generation);
    // Rejects new leases immediately. Busy retains all resources for live leases;
    // retry on the UI thread after those leases are released. No method waits.
    SurfaceError Retire(JNIEnv *env);
    // On Busy or a framework error, the pointer remains valid for a retry.
    // Only successful Destroy consumes the handle and sets it to null.
    static SurfaceError Destroy(JNIEnv *env, SurfaceView *&surface);
    SurfaceStats Stats() const;

  private:
    explicit SurfaceView(std::shared_ptr<SurfaceState> state);
    ~SurfaceView() = default;
    std::shared_ptr<SurfaceState> state_;
};
} // namespace AndSwapChainHook::Android
