#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    // Android AArch64 inline hooks only. Installation requires exclusive ownership
    // of each target's bytes, page permissions, mapping lifetime and entry threads.
    // All target entry threads must be parked. Shared pages are coordinated only
    // within this component instance; foreign changes are rejected.
    // Negative errno results are observable failures; non-null *original on failure
    // means DobbyDestroy is required and must be retried until it succeeds.
    // Target bytes and exact original page permissions are checked on restoration.
    // All acquired replacement/original addresses must be permanently retired before
    // DobbyDestroy. Stopping callbacks alone never establishes safe DSO unloading.
    // At most 256 installation attempts allocate nodes/code, retained until process
    // exit. BTI/PAC entry sequences, PC-relative literals and branches into the
    // patched window are unsupported. Near branches, symbol resolution
    // and instruction instrumentation are outside this facade.
    int DobbyHook(void *address, void *replacement, void **original);
    int DobbyDestroy(void *address);
    const char *DobbyBuildRevision(void);
#ifdef __cplusplus
}
#endif
