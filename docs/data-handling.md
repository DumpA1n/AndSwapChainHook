# Data handling

## Actual data access

Memory/ELF utilities inspect native addresses, mappings and module metadata. Input hooks observe host input; ImGui and configuration state can include user-entered text. Logging can include addresses, module names, package identifiers and diagnostic values. Runtime strings remain unchanged in this documentation phase.

Configuration storage uses `<EXTERNAL_STORAGE>/Android/data/<getprogname()>/cache`, with `/sdcard` as the base fallback. Active filenames and marker names derive from `kPROJECT_NAME`. Filenames are not protected by a complete path-containment policy, and updates are not transactional.

Logger paths depend on the compiled logger implementation and project macros; the adjacent AndCommon copy and UE's pinned dependency are not interchangeable. Logcat, per-tag files and buffered timeline files are separate output channels. Write failure, rotation or process termination can lose records; logs are not a complete audit trail.

Source: [ConfigStorage.cpp](../source/Utils/Config/ConfigStorage.cpp), [Logger.h](../source/Utils/Logger.h), [CrashHandler.cpp](../source/Utils/CrashHandler/CrashHandler.cpp), [InputEventHook.cpp](../source/InputEvent/InputEventHook.cpp).

## Handling expectations and missing controls

There is no verified repository-wide redaction, deletion or retention service. Review and minimize logs/dumps before sharing; remove the specifically identified experiment outputs when they are no longer needed. Identity originals, credentials, customer authorization documents and unapproved third-party data must remain outside Git and public reports. No cleanup command here authorizes deleting unrelated application data.

The review did not dynamically monitor network traffic and does not claim that execution is offline or free of external transmission. Model-assisted work can itself transmit supplied content to the selected platform. Approval for cyber access does not establish permission to disclose private source/data or determine account retention settings. Those require the actual task's data permissions and account configuration.
