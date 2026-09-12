# Third-party provenance

This inventory records checked-in dependency snapshots and the supported component graph. It is not a complete release SBOM. The two legacy gitlink revisions are unchanged.

Memory, Analysis, Input, Instrumentation, CrashRecorder and the explicit SurfaceView provider have no bundled third-party runtime implementation. Optional Graphics links the fixed Dobby source package below and system Android/EGL libraries. The optional HookAdapter links a consumer-selected PointerHook::Core package; no PointerHook source copy remains. KittyMemory, KittyMemoryEx and ImGui snapshots are retained for source provenance but are not compiled by the advertised components. SurfaceView creation and retirement use selected first-party framework ownership logic extracted from the maintainer's integration code; private profiles, experiments and hidden framework-record operations are excluded.

## Git submodules

| Path | Pinned commit | Configured source |
|---|---|---|
| `external/KittyMemory` | `4bc7691498d94a521884f6ff3327cbe56cb5437c` | `https://github.com/MJx0/KittyMemory.git` |
| `external/KittyMemoryEx` | `aa128114d98cc31fbe0955e9820b91657c6cb87d` | `https://github.com/MJx0/KittyMemoryEx.git` |

Branch labels in .gitmodules do not replace the gitlink pin. Nested dependencies and license files must be resolved at those exact commits for a release. AndUEProber pins its own AndSwapChainHook snapshot; the adjacent checkout is not its build input.

## Vendored snapshots

| Directory | Git tree ID | License evidence in this checkout |
|---|---|---|
| `external/imgui` | `88372c73ade0fa44fea7424dc2e2d1f94d07c73c` | [external/imgui/LICENSE.txt](external/imgui/LICENSE.txt); [external/imgui/examples/libs/glfw/COPYING.txt](external/imgui/examples/libs/glfw/COPYING.txt) |

Tree IDs identify repository snapshots. They do not establish an upstream release, absence of local modifications, or license completeness. Vendored content and embedded notices have not been rewritten.

## Dobby source package

| Item | Fixed identity |
|---|---|
| Official repository | `https://github.com/jmpews/Dobby` |
| Upstream commit | `5dfc8546954ce3b3198132ab13fddb89ee92cdd7` (2024-03-14) |
| Public source archive SHA-256 | `01a417233c40929aa21098c8a9724c00f6f05b5cde3f11abcac84b8e00053748` |
| License | [Apache-2.0](cmake/dobby/LICENSE), with upstream embedded notices preserved in retrieved source |
| Local build/patch | [Hook-only package and contract](cmake/dobby/README.md), [ownership.patch](cmake/dobby/ownership.patch) |
| Target | `Dobby::Dobby`, `ANDSWAPCHAIN_DOBBY_CONTRACT_VERSION=1` |

The supported graph contains no prebuilt Dobby archive. The source package exports only inline hook installation/restoration and build identity, with explicit permission ownership and partial-failure retention. Runtime/error-path evidence and matching artifact hashes are in [verification.json](tests/verification.json). Consumer-provided Dobby targets require their own matching source, contract and evidence; a target name is not provenance.

## AArch64 language tools

The assembler and disassembler are separate optional packages. The default build links neither engine. `ANDSWAPCHAIN_BUILD_ASSEMBLER=ON` selects Keystone, `ANDSWAPCHAIN_BUILD_DISASSEMBLER=ON` selects Capstone, and `ANDSWAPCHAIN_BUILD_ASSEMBLE=ON` selects both. Generated instructions are data; the components do not execute them.

| Engine | Pinned upstream source | Archive SHA-256 | License evidence |
|---|---|---|---|
| Keystone | `keystone-engine/keystone`, `dc7932ef2b2c4a793836caec6ecab485005139d6` | `23aac537d6a596b1b9a597219fd30ee0c3a0955336b3d20df408aed432b9bcf8` | [GPL version 2](cmake/analysis/licenses/keystone/COPYING), [client exceptions](cmake/analysis/licenses/keystone/EXCEPTIONS-CLIENT), [commercial license notice](cmake/analysis/licenses/keystone/LICENSE-COM.TXT), nested LLVM notices under [cmake/analysis/licenses/keystone/llvm](cmake/analysis/licenses/keystone/llvm) |
| Capstone | `capstone-engine/capstone`, `08a06aa38d093f99d83643dacde74cba0495d9e3` | `a6ee963af147e567308bad37dca584dbcd9c4208857d2bb8894b445950d354ba` | [engine license](cmake/analysis/licenses/capstone/LICENSE.TXT), [BSD 3-clause text](cmake/analysis/licenses/capstone/LICENSE_BSD_3_CLAUSE.txt), [LLVM notice](cmake/analysis/licenses/capstone/LICENSE_LLVM.TXT) |

Both engines build from hash-checked source with only AArch64 enabled. Keystone uses checked-in target tables, current CMake policies, and Android AArch64 host identification through the [CMake compatibility patch](cmake/analysis/patches/KeystoneCompatibility.cmake); the patch and notices are installed alongside the package. Capstone uses its own `6.0.0` project version and is compiled without a local source patch. Installed archives are named `libandswapchain_keystone.a` and `libandswapchain_capstone.a`; public dependency targets are `Keystone::Keystone` and `Capstone::Capstone`. Consumer-provided targets supply their own compatible engine, provenance, and notices.

The first-party MIT license does not relicense either engine. The Keystone opt-in selects the bundled GPL version 2 terms unless the distributor separately obtains a commercial license. Installed notices reside in `share/licenses/AndSwapChainHook`; exact verified source and artifact identities are recorded in the `assembly_validation` object of [verification.json](tests/verification.json).

## Source attribution gaps

Directory names identify components but do not prove the upstream revision or local patch set. The inventory does not infer a legal incompatibility from missing files. Before distributing binaries, resolve source/build provenance and include the required notices for all linked and nested components. Existing MIT licenses in public repositories remain unchanged.
