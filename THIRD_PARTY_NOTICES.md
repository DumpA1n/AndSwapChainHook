# Third-party provenance

Inventory baseline: `90fd30add8d9b14a94489fda5160d13ea7decd30`. This records the checked-in snapshot, not a complete release SBOM. Dependency revisions, URLs and build inputs are unchanged.

## Git submodules

| Path | Pinned commit | Configured source |
|---|---|---|
| `external/KittyMemory` | `4bc7691498d94a521884f6ff3327cbe56cb5437c` | `https://github.com/MJx0/KittyMemory.git` |
| `external/KittyMemoryEx` | `aa128114d98cc31fbe0955e9820b91657c6cb87d` | `https://github.com/MJx0/KittyMemoryEx.git` |

Branch labels in .gitmodules do not replace the gitlink pin. Nested dependencies and license files must be resolved at those exact commits for a release. AndUEProber pins its own AndSwapChainHook snapshot; the adjacent checkout is not its build input.

## Vendored snapshots

| Directory | Git tree ID | License evidence in this checkout |
|---|---|---|
| `external/Dobby` | `f175e745abfd238c768bb31bb578324983e9cebc` | No standalone license file inventoried; inspect embedded notices and recover upstream provenance before redistribution |
| `external/imgui` | `88372c73ade0fa44fea7424dc2e2d1f94d07c73c` | [external/imgui/LICENSE.txt](external/imgui/LICENSE.txt); [external/imgui/examples/libs/glfw/COPYING.txt](external/imgui/examples/libs/glfw/COPYING.txt) |

Tree IDs identify repository snapshots. They do not establish an upstream release, absence of local modifications, or license completeness. Vendored content and embedded notices have not been rewritten.

## Prebuilt archives

| Archive | SHA-256 | Provenance limit |
|---|---|---|
| `external/Dobby/arm64-v8a/libdobby.a` | `786c2b23411ad4aeb3f9c68363d409f0b16b6dc33d448a7566ea68742e382ab3` | Upstream source revision, build recipe/toolchain and archive-specific license completeness are not verified |

## Source attribution gaps

Directory names identify components but do not prove the upstream revision or local patch set. The inventory does not infer a legal incompatibility from missing files. Before distributing binaries, resolve source/build provenance and include the required notices for all linked and nested components. Existing MIT licenses in public repositories remain unchanged.
