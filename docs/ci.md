# Continuous integration scope

[The build workflow](../.github/workflows/build.yml) uses Ubuntu 24.04, commit-pinned GitHub actions, read-only repository permissions, and no persisted checkout credentials. Android jobs select NDK `29.0.14206865`, `arm64-v8a`, and API 27.

The default host Debug, Release, and sanitizer jobs execute only the CTest selector `^andswapchain_(input|headers)$`. The Release job also compiles and runs independent source and installed public consumers. Default Android Debug and Release jobs cross-build the default targets, public header checks, and both consumer forms; they do not execute Android binaries.

The separate optional language-tool job calls `tests/verify_assemble.py` without `--device`. It tests host Debug/Release assembly and disassembly fixtures, performs Android Debug/Release cross-builds, and builds all three language-tool components as source and installed consumers. Source archive digests, generated dependency configuration, license correspondence, source-file digests, artifact digests, and per-command logs are recorded in its uploaded `assembly_validation` evidence. Device execution is explicitly unverified in that job.

The workflow does not execute `andswapchain_contracts`, Dobby, graphics, hook installation, permission changes, crash-recording, or device lifecycle fixtures. Existing test definitions and manual commands remain available; default build success is not runtime coverage for those tests. Language-tool contract substitutes exercise only assembler/disassembler wrapper errors and resource ownership.

Workflow execution on GitHub Actions is unverified. Local YAML/shell validation and selected command checks are recorded in [ci-validation.json](../tests/ci-validation.json). The existing [verification manifest](../tests/verification.json) contains separate local and device evidence, which does not establish a successful CI run.
