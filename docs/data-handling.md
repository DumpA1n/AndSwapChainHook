# Data handling

The supported analysis session reads an explicitly supplied range in its own process and retains match addresses and structured progress/errors in memory. `ASC_Snapshot` copies values into caller-owned storage. It does not export a file, load configuration, upload results or install process-wide diagnostics. `ASC_Destroy` cancels and joins the session worker and releases its read channel.

The Input component retains copied pointer coordinates and event metadata in a fixed queue until consumption or reset. It neither hooks a framework input function nor accesses ImGui directly.

The caller controls whether results or callbacks are logged or persisted. No repository-wide redaction, deletion or retention service is provided. Minimize addresses, module identities and other target data before external disclosure. Credentials, identity originals and private authorization records do not belong in this public repository.

The optional Dobby-backed EGL component uses a fixed source facade with logging disabled. Source, local patch, artifact digests and retained allocation limits are recorded in [third-party notices](../THIRD_PARTY_NOTICES.md). Explicit installation reads process mappings and temporarily adds write permission only to owned pages; errors and incomplete restoration remain observable. The optional SurfaceView component retains Activity, WindowManager, SurfaceView and ANativeWindow resources until explicit successful retirement. It does not intercept input.

The optional CrashRecorder duplicates a caller-supplied writable descriptor and retains that descriptor for process lifetime, including after restoration. Each binary record contains signal/code, fault address and available PC/SP/LR fields; offline handling is the caller's responsibility. No signal recorder is included in the default DSO.

Legacy configuration/logger/input sources are not part of advertised targets. Their source-level output paths and limitations must be reviewed before any downstream reintroduction; their presence does not establish transactional persistence, redaction or signal safety.

The inert-loading device fixture observes threads, file descriptors, local directory entries, dumpability and selected signal handlers. It is not a network-capture audit or proof about arbitrary downstream composition. Sharing model-assisted work requires the actual source/data permissions and account retention configuration.
