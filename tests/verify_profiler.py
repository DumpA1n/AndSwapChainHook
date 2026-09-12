#!/usr/bin/env python3
"""Verify the standalone Profiler and record only its own source and artifact evidence."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/profiler-validation"


def digest(path):
    try:
        name = str(path.relative_to(ROOT))
    except ValueError:
        name = str(path)
    return {"path": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=BUILD,
                        help="Output directory for independent producer and consumer builds")
    parser.add_argument("--common-source", type=Path,
                        help="Optional Common checkout for its thin compatibility-header fixture")
    parser.add_argument("--android-assembly-build", type=Path,
                        help="Existing validated Android assembly producer to augment and install at a new prefix")
    args = parser.parse_args()
    build_root = args.build_dir.resolve()
    logs = build_root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / name for name in (
        "CMakeLists.txt", "README.md", "cmake/AndSwapChainHookConfig.cmake.in",
        "include/AndSwapChainHook/Profiler.h", "source/Diagnostics/Profiler.cpp",
        "tests/profiler.cpp", "tests/profiler_consumer/CMakeLists.txt",
        "tests/profiler_consumer/main.cpp", "tests/verify_profiler.py", "docs/profiler.md",
    )]
    if args.common_source:
        args.common_source = args.common_source.resolve()
        sources.extend(args.common_source / name for name in (
            "source/Utils/Profiler.h", "source/Utils/SnapshotBuffer.h",
            "source/Graphics/SurfaceGeometry.h", "tests/host/contracts.cpp",
            "tests/host/stubs/Utils/Logger.h", "tests/host/identity_peer.cpp",
            "source/ImGui/OverlayIdentity.h",
        ))
    source_hashes = [digest(path) for path in sources]
    checks = []
    artifacts = set()
    component_options = [f"-DANDSWAPCHAIN_BUILD_{component}=OFF" for component in (
        "ANDROID", "GRAPHICS", "OVERLAY", "CRASH_RECORDER", "POINTER_ADAPTER",
        "ASSEMBLE", "ASSEMBLER", "DISASSEMBLER",
    )]

    def execute(name, command):
        command = [str(item) for item in command]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        log = logs / (name + ".log")
        log.write_text(result.stdout + result.stderr)
        checks.append({"name": name, "command": command,
                       "status": "passed" if result.returncode == 0 else "failed",
                       "exit_code": result.returncode, "log": digest(log)})
        print(name + ": " + checks[-1]["status"].upper(), flush=True)
        if result.returncode:
            print(result.stdout[-4000:] + result.stderr[-4000:])
            raise RuntimeError(name)
        return result.stdout.strip()

    failure = None
    toolchain = {}
    android_package = None
    try:
        toolchain["host"] = platform.platform()
        toolchain["cmake"] = execute("cmake-version", ["cmake", "--version"])
        toolchain["compiler"] = execute("compiler-version", ["c++", "--version"])
        for name in ("debug", "release", "tsan"):
            build = build_root / name
            options = ["-DCMAKE_BUILD_TYPE=" + ("Release" if name == "release" else "Debug"),
                       "-DANDSWAPCHAIN_ENABLE_SANITIZERS=" + ("ON" if name == "debug" else "OFF")]
            if name == "tsan":
                options += ["-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer",
                            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread"]
            execute(name + "-configure", ["cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
                    "-DBUILD_TESTING=ON", *component_options, *options])
            execute(name + "-build", ["cmake", "--build", build, "--parallel", "4", "--target",
                    "andswapchain_profiler_tests", "andswapchain_header_Profiler"])
            execute(name + "-ctest", ["ctest", "--test-dir", build, "--output-on-failure",
                    "--no-tests=error", "-R", "^andswapchain_profiler$"])
            artifacts.update((build / "libAndSwapChainHookProfiler.a",
                              build / "andswapchain_profiler_tests"))

        source = build_root / "source-consumer"
        prefix = build_root / "relocated-install"
        execute("source-consumer-configure", ["cmake", "-S", ROOT / "tests/profiler_consumer",
                "-B", source, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                f"-DANDSWAPCHAIN_SOURCE_DIR={ROOT}", f"-DCMAKE_INSTALL_PREFIX={build_root / 'original-install'}"])
        execute("source-consumer-build", ["cmake", "--build", source, "--parallel", "4"])
        execute("source-consumer-ctest", ["ctest", "--test-dir", source, "--output-on-failure", "--no-tests=error"])
        execute("install", ["cmake", "--install", source, "--prefix", prefix])
        installed = build_root / "installed-consumer"
        execute("installed-consumer-configure", ["cmake", "-S", ROOT / "tests/profiler_consumer",
                "-B", installed, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={prefix}"])
        execute("installed-consumer-build", ["cmake", "--build", installed, "--parallel", "4"])
        execute("installed-consumer-ctest", ["ctest", "--test-dir", installed, "--output-on-failure", "--no-tests=error"])
        artifacts.update((source / "profiler_consumer", installed / "profiler_consumer",
                          prefix / "lib/libAndSwapChainHookProfiler.a",
                          prefix / "include/AndSwapChainHook/Profiler.h"))
        artifacts.update((prefix / "lib/cmake/AndSwapChainHook").glob("*.cmake"))

        if args.common_source:
            compatibility = build_root / "common-compatibility"
            compatibility.mkdir(parents=True, exist_ok=True)
            cmake_file = compatibility / "CMakeLists.txt"
            common = args.common_source.as_posix()
            cmake_file.write_text(f'''cmake_minimum_required(VERSION 3.22.1)
project(CommonProfilerCompatibility LANGUAGES CXX)
find_package(AndSwapChainHook 0.2 CONFIG REQUIRED COMPONENTS Profiler)
add_executable(common_profiler_contracts "{common}/tests/host/contracts.cpp"
    "{common}/tests/host/identity_peer.cpp")
target_include_directories(common_profiler_contracts PRIVATE "{common}/tests/host/stubs" "{common}/source" "{common}/external")
target_compile_options(common_profiler_contracts PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(common_profiler_contracts PRIVATE -fsanitize=address,undefined)
target_link_libraries(common_profiler_contracts PRIVATE AndSwapChainHook::Profiler)
enable_testing()
add_test(NAME common_profiler_compatibility COMMAND common_profiler_contracts)
set_tests_properties(common_profiler_compatibility PROPERTIES TIMEOUT 60)
''')
            consumer = compatibility / "build"
            execute("common-compatibility-configure", ["cmake", "-S", compatibility, "-B", consumer,
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", f"-DCMAKE_PREFIX_PATH={prefix}"])
            execute("common-compatibility-build", ["cmake", "--build", consumer, "--parallel", "4"])
            execute("common-compatibility-ctest", ["ctest", "--test-dir", consumer,
                    "--output-on-failure", "--no-tests=error"])
            artifacts.update((cmake_file, consumer / "common_profiler_contracts"))
        if args.android_assembly_build:
            android_build = args.android_assembly_build.resolve()
            cache = (android_build / "CMakeCache.txt").read_text()
            if "ANDROID_ABI:UNINITIALIZED=arm64-v8a" not in cache and "ANDROID_ABI:STRING=arm64-v8a" not in cache:
                raise RuntimeError("The selected Android producer does not identify arm64-v8a")
            android_prefix = build_root / "android-complete-install"
            execute("android-profiler-build", ["cmake", "--build", android_build,
                    "--parallel", "4", "--target", "AndSwapChainHookProfiler"])
            execute("android-complete-install", ["cmake", "--install", android_build,
                    "--prefix", android_prefix])
            required = ("Memory", "Analysis", "Instrumentation", "Input", "Assembler", "Disassembler", "Profiler")
            for component in required:
                archive = android_prefix / "lib" / ("libAndSwapChainHook" + component + ".a")
                if not archive.is_file():
                    raise RuntimeError("Missing installed component: " + component)
                artifacts.add(archive)
            artifacts.update((android_prefix / "lib").glob("libandswapchain_*.a"))
            artifacts.update((android_prefix / "lib/cmake").rglob("*.cmake"))
            artifacts.add(android_prefix / "include/AndSwapChainHook/Profiler.h")
            android_package = {"status": "built and installed; runtime unverified",
                               "prefix": str(android_prefix), "producer_build": str(android_build),
                               "components": list(required), "android_abi": "arm64-v8a"}
        if source_hashes != [digest(path) for path in sources]:
            raise RuntimeError("Profiler source inputs changed during validation")
    except (OSError, RuntimeError) as error:
        failure = str(error)
    manifest = {
        "schema": 1,
        "component": "AndSwapChainHook::Profiler",
        "status": "failed" if failure else "passed",
        "toolchain": toolchain,
        "checks": checks,
        "source_hashes": source_hashes,
        "source_hashes_after": [digest(path) for path in sources],
        "source_consistent": source_hashes == [digest(path) for path in sources],
        "artifacts": [digest(path) for path in sorted(artifacts) if path.is_file()],
        "limits": {"zone_capacity": 96, "maximum_name_bytes": 95, "history_samples": 128},
        "android_package": android_package,
        "limitations": [
            "Host execution only; Android execution is not part of this evidence.",
            "Counters and history elements are atomic; multi-field snapshots and reset are not coherent.",
            "Separate static component copies do not share registry state.",
            "Other assembly, CI, instrumentation, and device evidence is not refreshed by this run.",
            *([] if args.common_source else ["Common compatibility fixture was not requested."]),
        ],
    }
    if failure:
        manifest["failure"] = failure
    (ROOT / "tests/profiler-verification.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if failure:
        raise SystemExit(failure)


if __name__ == "__main__":
    main()
