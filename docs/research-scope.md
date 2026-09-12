# Research scope

AndSwapChainHook supplies bounded process analysis, explicit EGL instrumentation and a normal-framework SurfaceView provider for owned or explicitly authorized applications. Target permission is supplied by the operator; package names, repository visibility and JNI reserved values do not authenticate a target.

## Current operation boundaries

Loading the analysis DSO is inert. `ASC_Initialize` explicitly opens only the current process's read channel. `ASC_Start` accepts an address range, copied pattern, byte/match budgets and deadline. Cancellation and stop are explicit. Read failures are observable and do not trigger dumpability changes, permission modification or a stronger backend.

Optional EGL installation modifies executable instructions through Dobby. Installation requires ownership of target bytes, page permissions and mapping lifetime, plus external entry quiescence. Owned presentation entry tables are explicit; system exports with PAC/BTI entries are unsupported. Restoration additionally requires retirement of all acquired replacement/original trampoline addresses; stopping callbacks is a weaker operation than restoring patches or unloading the DSO. The host Vulkan provider reports unsupported.

The optional SurfaceView provider receives the Activity and current UI-thread JNIEnv explicitly. It uses WindowManager.addView/removeViewImmediate and retains framework ownership records. Window leases prevent resource release while a renderer still uses a generation. It neither discovers third-party Activities nor implements SurfaceFlinger concealment.

Optional CrashRecorder writes bounded binary signal records to an explicitly supplied sink. Installation, old dispositions, sink retention and alternate stacks have explicit ownership; it does not recover arbitrary engine execution.

Retained legacy input, UI, configuration and native utility sources are outside advertised CMake components and still require the remaining refactor. Their presence is not evidence that their lifecycle or API contracts are verified.

## Evidence scope

The owned native fixtures use only their own memory, EGL context and process. The APK `com.andswapchain.owned` is built from repository test sources. No third-party application is inspected by these fixtures. A separate runtime record is required for any additional target, operation, device or output scope.

Source implementation, compilation, device execution and unavailable validation are distinct states. The repository does not claim CVP/TAC approval, software certification or authorization for a third-party application.
