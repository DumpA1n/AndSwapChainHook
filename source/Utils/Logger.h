#pragma once

// =============================================================================
// Logger provides three call interfaces.
//
// A) printf wrappers call __android_log_print directly; logd can drop messages.
//        LOGI / LOGD / LOGW / LOGE / LOGF
// LOGD prefixes the source file, line and function using __FILE_NAME__.
//
// B) Categories use BufferedLog with optional per-tag files; writes are fallible.
// DECLARE_LOG_CATEGORY(LogTemp, "Temp"); // Main timeline only.
// DECLARE_LOG_CATEGORY_FILE(LogVM, "VM", "AceVM"); // Timeline and per-tag file.
//
// Object methods:
//        LogVM.Info  ("loaded {} symbols", n);
//        LogVM.Debug ("step={} pc={:#x}", op, pc);
// LogVM.FatalSync("crash code={}", code); // Request immediate flush.
//
// Macros can compile out expensive argument evaluation.
//        LOG_DEBUG     (LogVM, "step={}", op);
//        LOG_FATAL_SYNC(LogVM, "crash {}", code);
//
// C) FLOG compatibility interface.
//
// Levels use ANDROID_LOG_* numeric values.
// kNO_DEBUG_LOG discards Debug method bodies with if constexpr.
//
// Backend components:
// BufferedLog::Emit/Flush: timeline buffering and rotation.
// LogFile and GetLogFile: per-tag files.
// FormatedTime and FormatedTimeShort: timestamp helpers.
// =============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef kANDROID_LOG
#include <android/log.h>
#else
// Define Android priority constants for non-Android compilation.
// Values match android_LogPriority in android/log.h.
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO  4
#define ANDROID_LOG_WARN  5
#define ANDROID_LOG_ERROR 6
#define ANDROID_LOG_FATAL 7
#endif

#ifndef kANDROID_LOG_TAG
#define kANDROID_LOG_TAG "INJECT"
#endif

#ifndef kPROJECT_NAME
#define kPROJECT_NAME "dfmhook"
#endif

// =============================================================================
// Timestamp helpers.
// =============================================================================
inline std::string FormatedTime()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_r(&time_t_now, &local_tm);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

inline std::string FormatedTimeShort()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_r(&time_t_now, &local_tm);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%m-%d_%H-%M-%S");
    return oss.str();
}

// =============================================================================
// LogFile provides per-tag files for categories and crash reports.
// Append mode writes at the end of the file without explicit seeking.
// =============================================================================
class LogFile {
public:
    explicit LogFile(std::filesystem::path path) : path_(std::move(path)) {
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
        file_.open(path_, std::ios::binary | std::ios::app);
    }

    void Append(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.write(data.data(), data.size());
            file_.flush();
        }
    }

    template <typename... Args>
    void Append(std::format_string<Args...> fmt, Args&&... args) {
        Append(std::format(fmt, std::forward<Args>(args)...));
    }

private:
    std::filesystem::path path_;
    std::ofstream         file_;
    std::mutex            mutex_;
};

inline LogFile* GetLogFile(const std::string& dir)
{
    namespace fs = std::filesystem;
    static std::unordered_map<std::string, std::unique_ptr<LogFile>> g_log_files;
    static std::mutex g_log_files_mutex;
    std::lock_guard<std::mutex> lock(g_log_files_mutex);
    auto [it, inserted] = g_log_files.try_emplace(dir, nullptr);
    if (inserted)
    {
        const char* ext = std::getenv("EXTERNAL_STORAGE");
        fs::path saved_path = fs::path(ext && ext[0] ? ext : "/sdcard") / "Android" / "data" / getprogname() / "cache";
        fs::path final_path = saved_path / kPROJECT_NAME / dir;
        fs::path file_name = dir + "_" + FormatedTimeShort() + ".log";
        it->second = std::make_unique<LogFile>(final_path / file_name);
    }
    return it->second.get();
}

// =============================================================================
// BufferedLog: category timeline backend.
//
// One mutex protects the buffer and file handle.
// Flush at kFlushThreshold (16 KB) to batch writes.
// Write to logd after unlocking; delivery is best effort.
// Rotate at kPerFileMaxBytes (32 MB), retaining at most
// kMaxFilesPerSession files; file output stops at the cap while logd continues.
// Startup cleanup retains the newest kRetainTotalFiles Logger_*.log files.
// Initialize State on first Emit; register an exit flush once.
// Path: /sdcard/Android/data/<pkg>/cache/<PROJECT>/Logger/Logger_<ts>_<seq>.log.
//
// Inline functions and their named State type share storage across
// translation units under the C++ inline-definition rules.
// =============================================================================
namespace BufferedLog {
namespace detail {

constexpr size_t kFlushThreshold     = 16 * 1024;
constexpr size_t kPerFileMaxBytes    = 32 * 1024 * 1024;
constexpr int    kMaxFilesPerSession = 4;
constexpr int    kRetainTotalFiles   = 12;

inline char PrioChar(int prio)
{
#ifdef kANDROID_LOG
    switch (prio)
    {
        case ANDROID_LOG_DEBUG: return 'D';
        case ANDROID_LOG_INFO:  return 'I';
        case ANDROID_LOG_WARN:  return 'W';
        case ANDROID_LOG_ERROR: return 'E';
        case ANDROID_LOG_FATAL: return 'F';
        default:                return '?';
    }
#else
    (void)prio; return '?';
#endif
}

// Millisecond timestamp for ordering and logcat correlation.
inline size_t WritePrefix(char* out, size_t cap, int prio)
{
    using namespace std::chrono;
    auto now    = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    auto ms     = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm;
    localtime_r(&t, &tm);
    return static_cast<size_t>(std::snprintf(
        out, cap, "[%c %02d:%02d:%02d.%03lld] ",
        PrioChar(prio), tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<long long>(ms)));
}

class State
{
public:
    static State& Instance()
    {
        static State* inst = new State();  // Retained for process lifetime to avoid static-destruction ordering hazards.
        return *inst;
    }

    void Emit(int prio, std::string_view msg)
    {
        // Append to the memory buffer.
        {
            std::lock_guard<std::mutex> lk(mu_);

            char head[40];
            size_t n = WritePrefix(head, sizeof(head), prio);
            if (n > sizeof(head)) n = sizeof(head);
            buffer_.append(head, n);
            buffer_.append(msg.data(), msg.size());
            if (msg.empty() || msg.back() != '\n')
                buffer_.push_back('\n');

            if (buffer_.size() >= kFlushThreshold)
                FlushLocked();
        }

        // Best-effort logd write outside the mutex.
#ifdef kANDROID_LOG
        __android_log_print(prio, kANDROID_LOG_TAG, "%.*s",
                            static_cast<int>(msg.size()), msg.data());
#endif
    }

    void Flush()
    {
        std::lock_guard<std::mutex> lk(mu_);
        FlushLocked();
    }

private:
    State()
    {
        InitDirAndTimestamp();
        CleanupOldFiles();
        OpenSeqFile(1);
        std::atexit(&State::AtExit);
    }

    static void AtExit() { Instance().Flush(); }

    void InitDirAndTimestamp()
    {
        const char* ext = std::getenv("EXTERNAL_STORAGE");
        dir_ = std::filesystem::path(ext && *ext ? ext : "/sdcard")
            / "Android" / "data" / getprogname() / "cache" / kPROJECT_NAME / "Logger";

        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);

        std::time_t t = std::time(nullptr);
        std::tm tm;
        localtime_r(&t, &tm);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%m-%d_%H-%M-%S", &tm);
        timestamp_ = ts;
    }

    void CleanupOldFiles()
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        std::vector<std::pair<fs::file_time_type, fs::path>> files;
        for (const auto& entry : fs::directory_iterator(dir_, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            const auto name = entry.path().filename().string();
            if (name.size() < std::string_view("Logger_X.log").size()) continue;
            if (name.compare(0, 7, "Logger_") != 0) continue;
            if (name.compare(name.size() - 4, 4, ".log") != 0) continue;
            auto mtime = entry.last_write_time(ec);
            if (ec) { ec.clear(); continue; }
            files.emplace_back(mtime, entry.path());
        }

        if (files.size() <= kRetainTotalFiles) return;

        std::sort(files.begin(), files.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        const size_t toDelete = files.size() - kRetainTotalFiles;
        for (size_t i = 0; i < toDelete; ++i)
        {
            fs::remove(files[i].second, ec);
            ec.clear();
        }
    }

    void OpenSeqFile(int seq)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "Logger_%s_%03d.log", timestamp_.c_str(), seq);
        std::filesystem::path full = dir_ / name;
        file_ = std::fopen(full.c_str(), "ab");
        seq_ = seq;
        bytes_in_current_file_ = 0;
    }

    void RotateIfNeeded()
    {
        if (!file_) return;
        if (bytes_in_current_file_ < kPerFileMaxBytes) return;

        std::fclose(file_);
        file_ = nullptr;

        if (seq_ >= kMaxFilesPerSession)
        {
#ifdef kANDROID_LOG
            __android_log_print(ANDROID_LOG_WARN, kANDROID_LOG_TAG,
                "[BufferedLog] session hit %d x %zu MB cap; further file writes dropped, logd still active",
                kMaxFilesPerSession, kPerFileMaxBytes / (1024 * 1024));
#endif
            return;
        }
        OpenSeqFile(seq_ + 1);
    }

    void FlushLocked()
    {
        if (buffer_.empty()) return;
        if (file_)
        {
            std::fwrite(buffer_.data(), 1, buffer_.size(), file_);
            std::fflush(file_);
            bytes_in_current_file_ += buffer_.size();
            RotateIfNeeded();
        }
        buffer_.clear();
    }

    std::FILE*            file_ = nullptr;
    std::string           buffer_;
    std::mutex            mu_;
    std::filesystem::path dir_;
    std::string           timestamp_;
    int                   seq_ = 0;
    size_t                bytes_in_current_file_ = 0;
};

}  // namespace detail

inline void Emit(int android_priority, std::string_view msg)
{
    detail::State::Instance().Emit(android_priority, msg);
}

inline void Flush()
{
    detail::State::Instance().Flush();
}

}  // namespace BufferedLog

// =============================================================================
// LogHelper utilities for FLOG and categories.
// =============================================================================
namespace LogHelper {

// Map Android priorities to letters for per-tag files and non-Android output.
inline char prioChar(int prio) {
    switch (prio) {
        case ANDROID_LOG_DEBUG: return 'D';
        case ANDROID_LOG_INFO:  return 'I';
        case ANDROID_LOG_WARN:  return 'W';
        case ANDROID_LOG_ERROR: return 'E';
        case ANDROID_LOG_FATAL: return 'F';
        default:                return '?';
    }
}

// kNO_DEBUG_LOG discards Debug method bodies at compile time.
constexpr bool isCompiledOut(int prio) {
#ifdef kNO_DEBUG_LOG
    return prio == ANDROID_LOG_DEBUG;
#else
    (void)prio;
    return false;
#endif
}

// FLOG backend.
inline void emitStr(int prio, const char* msg) {
#ifdef kANDROID_LOG
    BufferedLog::Emit(prio, std::string_view(msg));
#else
    printf("%c: %s\n", prioChar(prio), msg);
#endif
}

// Category object interface.
//
// name supplies the tag in each timeline prefix.
// A nonempty file_tag also appends through GetLogFile(file_tag).
// Per-tag output includes timestamp, priority and message.
//
// Pass runtime strings as LogVM.Info("{}", runtime_str), because
// std::format_string requires a compile-time format string.
struct Category;

inline void emitCategory(const Category& cat, int prio, bool sync, const std::string& msg);

struct Category {
    const char* name;
    const char* file_tag;

#define LOGCAT_METHOD_(MethodName, Prio, Sync)                                             \
    template<typename... Args>                                                             \
    inline void MethodName(std::format_string<Args...> fmt, Args&&... args) const {        \
        if constexpr (!isCompiledOut(Prio)) {                                              \
            emitCategory(*this, (Prio), (Sync),                                            \
                std::format(fmt, std::forward<Args>(args)...));                            \
        }                                                                                  \
    }

    LOGCAT_METHOD_(Debug, ANDROID_LOG_DEBUG, false)
    LOGCAT_METHOD_(Info,  ANDROID_LOG_INFO,  false)
    LOGCAT_METHOD_(Warn,  ANDROID_LOG_WARN,  false)
    LOGCAT_METHOD_(Error, ANDROID_LOG_ERROR, false)
    LOGCAT_METHOD_(Fatal, ANDROID_LOG_FATAL, false)

    LOGCAT_METHOD_(DebugSync, ANDROID_LOG_DEBUG, true)
    LOGCAT_METHOD_(InfoSync,  ANDROID_LOG_INFO,  true)
    LOGCAT_METHOD_(WarnSync,  ANDROID_LOG_WARN,  true)
    LOGCAT_METHOD_(ErrorSync, ANDROID_LOG_ERROR, true)
    LOGCAT_METHOD_(FatalSync, ANDROID_LOG_FATAL, true)

#undef LOGCAT_METHOD_
};

inline void emitCategory(const Category& cat, int prio, bool sync, const std::string& msg) {
    // Write to the main timeline.
#ifdef kANDROID_LOG
    auto line = std::format("[{}] {}", cat.name, msg);
    BufferedLog::Emit(prio, line);
#else
    printf("%c: [%s] %s\n", prioChar(prio), cat.name, msg.c_str());
#endif

    // Write to the optional per-tag file.
    if (cat.file_tag) {
        GetLogFile(cat.file_tag)->Append("{}    [{}] {}\n",
            FormatedTime(), prioChar(prio), msg);
    }

    // Request a synchronous flush.
    if (sync) {
        BufferedLog::Flush();
    }
}

}  // namespace LogHelper

// =============================================================================
// printf wrappers call __android_log_print directly, bypassing BufferedLog.
// logd can drop messages; category buffering is also subject to file-write failures.
//
// LOGD source prefixes use compiler builtins.
// __FILE_NAME__ supplies the basename at compile time.
// Requires a compiler with __FILE_NAME__ support.
// =============================================================================
#ifdef kANDROID_LOG

#ifndef kNO_DEBUG_LOG
#define LOGD(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, kANDROID_LOG_TAG, "[%s(%d) %s] " fmt, __FILE_NAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
#define LOGD(fmt, ...) do {} while(0)
#endif

#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO,  kANDROID_LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN,  kANDROID_LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, kANDROID_LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...) __android_log_print(ANDROID_LOG_FATAL, kANDROID_LOG_TAG, fmt, ##__VA_ARGS__)

#else  // Without kANDROID_LOG, write to stdout.

#ifndef kNO_DEBUG_LOG
#define LOGD(fmt, ...) printf("D: [%s(%d) %s] " fmt "\n", __FILE_NAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
#define LOGD(fmt, ...) do {} while(0)
#endif
#define LOGI(fmt, ...) printf("I: " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) printf("W: " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) printf("E: " fmt "\n", ##__VA_ARGS__)
#define LOGF(fmt, ...) printf("F: " fmt "\n", ##__VA_ARGS__)

#endif  // kANDROID_LOG

// =============================================================================
// FLOG compatibility interface; categories also provide object methods.

// =============================================================================
#ifndef kNO_DEBUG_LOG
#define FLOGD(fmt, ...) LogHelper::emitStr(ANDROID_LOG_DEBUG, std::format(fmt, ##__VA_ARGS__).c_str())
#else
#define FLOGD(fmt, ...) do {} while(0)
#endif

#define FLOGI(fmt, ...) LogHelper::emitStr(ANDROID_LOG_INFO,  std::format(fmt, ##__VA_ARGS__).c_str())
#define FLOGW(fmt, ...) LogHelper::emitStr(ANDROID_LOG_WARN,  std::format(fmt, ##__VA_ARGS__).c_str())
#define FLOGE(fmt, ...) LogHelper::emitStr(ANDROID_LOG_ERROR, std::format(fmt, ##__VA_ARGS__).c_str())
#define FLOGF(fmt, ...) LogHelper::emitStr(ANDROID_LOG_FATAL, std::format(fmt, ##__VA_ARGS__).c_str())

// =============================================================================
// Category declarations and calls: object and macro interfaces.
//
// Declarations:
// DECLARE_LOG_CATEGORY(LogTemp, "Temp"); // Main timeline only.
// DECLARE_LOG_CATEGORY_FILE(LogVM, "VM", "AceVM"); // Also write a per-tag file.
//
// Both call interfaces use the same backend.
//
// Object methods:
//       LogVM.Debug ("step={} pc={:#x}", op, pc);
//       LogVM.Info  ("loaded {} symbols", n);
//       LogVM.Error ("fatal {}", x);
// LogVM.FatalSync("crash code={}", code); // Flush after writing.
//
// Macros can discard argument evaluation at compile time.
//       LOG_DEBUG(LogVM, "step={} pc={:#x}", op, pc);
//       LOG_FATAL_SYNC(LogVM, "crash code={}", code);
//
// Argument evaluation differs:
// kNO_DEBUG_LOG discards Debug backend calls in both interfaces.
// Macro arguments are inside the discarded if constexpr branch,
// so they are not evaluated.
// Object-method arguments are evaluated before entering the empty method.
// =============================================================================
#define DECLARE_LOG_CATEGORY(VarName, NameStr) \
    inline constexpr ::LogHelper::Category VarName{ NameStr, nullptr }

#define DECLARE_LOG_CATEGORY_FILE(VarName, NameStr, FileTag) \
    inline constexpr ::LogHelper::Category VarName{ NameStr, FileTag }

// Keep arguments inside if constexpr to discard disabled Debug evaluation.
#define LOG_INTERNAL_DISPATCH_(Cat, Prio, Sync, Fmt, ...) \
    do { \
        if constexpr (!::LogHelper::isCompiledOut(Prio)) { \
            ::LogHelper::emitCategory((Cat), (Prio), (Sync), \
                std::format(Fmt, ##__VA_ARGS__)); \
        } \
    } while(0)

#define LOG_DEBUG(Cat, Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_DEBUG, false, Fmt, ##__VA_ARGS__)
#define LOG_INFO(Cat,  Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_INFO,  false, Fmt, ##__VA_ARGS__)
#define LOG_WARN(Cat,  Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_WARN,  false, Fmt, ##__VA_ARGS__)
#define LOG_ERROR(Cat, Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_ERROR, false, Fmt, ##__VA_ARGS__)
#define LOG_FATAL(Cat, Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_FATAL, false, Fmt, ##__VA_ARGS__)

#define LOG_DEBUG_SYNC(Cat, Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_DEBUG, true, Fmt, ##__VA_ARGS__)
#define LOG_INFO_SYNC(Cat,  Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_INFO,  true, Fmt, ##__VA_ARGS__)
#define LOG_WARN_SYNC(Cat,  Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_WARN,  true, Fmt, ##__VA_ARGS__)
#define LOG_ERROR_SYNC(Cat, Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_ERROR, true, Fmt, ##__VA_ARGS__)
#define LOG_FATAL_SYNC(Cat, Fmt, ...) LOG_INTERNAL_DISPATCH_(Cat, ANDROID_LOG_FATAL, true, Fmt, ##__VA_ARGS__)
