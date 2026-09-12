#!/usr/bin/env python3
"""Record bounded language-tool package checks without replacing other evidence."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/assembly-validation"
LOGS = WORK / "logs"
COMPONENTS = ("Assembler", "Disassembler", "Assemble")
FIXTURES = (
    "andswapchain_assembler_roundtrip",
    "andswapchain_disassembler_roundtrip",
    "andswapchain_assemble_roundtrip",
    "andswapchain_assemble_contracts",
)


def digest(path):
    return {
        "path": str(path.relative_to(ROOT)),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, help="Android NDK for AArch64 API 27 cross-builds")
    parser.add_argument("--device", help="Explicit adb serial for owned byte fixtures")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel compilation jobs")
    args = parser.parse_args()
    if args.device and not args.ndk:
        parser.error("--device requires --ndk")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    LOGS.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / name for name in (
        "CMakeLists.txt", "CMakePresets.json", "README.md", "THIRD_PARTY_NOTICES.md",
        "cmake/AnalysisDependencies.cmake", "cmake/AndSwapChainHookConfig.cmake.in",
        "include/AndSwapChainHook/Assembler.h", "include/AndSwapChainHook/Disassembler.h",
        "tests/assemble_consumer/CMakeLists.txt", "tests/assembler_roundtrip.cpp",
        "tests/disassembler_roundtrip.cpp", "tests/assemble_roundtrip.cpp",
        "tests/assemble_contracts.cpp", "tests/verify_assemble.py",
    )]
    for directory in ("cmake/analysis", "source/Assemble"):
        sources.extend(path for path in (ROOT / directory).rglob("*") if path.is_file())
    source_hashes = [digest(path) for path in sorted(sources)]
    checks = []
    artifacts = set()
    dependencies = []
    dependency_configurations = []
    toolchain = {}
    remote = "/data/local/tmp/andswapchain-owned-language-fixtures"
    adb = ["adb", "-s", args.device] if args.device else None

    def execute(name, command):
        command = [str(item) for item in command]
        result = subprocess.run(command, cwd=ROOT, env=os.environ.copy(),
                                capture_output=True, text=True)
        log = LOGS / (name + ".log")
        log.write_text(result.stdout + result.stderr)
        check = {
            "name": name,
            "status": "passed" if result.returncode == 0 else "failed",
            "command": ["OWNED_DEVICE_SERIAL" if args.device and item == args.device
                        else item for item in command],
            "exit_code": result.returncode,
            "log": digest(log),
        }
        checks.append(check)
        print(name + ": " + check["status"].upper(), flush=True)
        if result.returncode:
            print(result.stdout[-4000:] + result.stderr[-4000:], file=sys.stderr)
            raise RuntimeError(name)
        return result.stdout.strip()

    def run_fixture(name, path, target_platform):
        artifacts.add(path)
        if target_platform == "host":
            execute(name, [path])
        elif adb:
            destination = remote + "/" + name
            execute(name + "-copy", [*adb, "push", path, destination])
            execute(name, [*adb, "shell", "chmod", "700", destination])
            execute(name + "-run", [*adb, "shell", "timeout", "30", destination])

    def verify_configuration(name, build, target_platform, component, output):
        configuration = {"name": name, "platform": target_platform, "component": component}
        if component != "Assembler":
            if "CAPSTONE_VERSION: 6.0.0" not in output:
                raise RuntimeError(name + ": Capstone must configure with its own 6.0.0 version.")
            configuration["capstone_project_version"] = "6.0.0"
        if component != "Disassembler":
            header = build / "_deps/andswapchain_keystone-build/llvm/include/llvm/Config/llvm-config.h"
            content = header.read_text()
            if '#define LLVM_DEFAULT_TARGET_TRIPLE "aarch64-linux-android"' not in content:
                raise RuntimeError(name + ": Keystone default target triple differs from AArch64.")
            if target_platform == "android" and '#define LLVM_HOST_TRIPLE "aarch64-linux-android"' not in content:
                raise RuntimeError(name + ": Keystone Android host triple differs from AArch64.")
            configuration["keystone_generated_configuration"] = digest(header)
        dependency_configurations.append(configuration)

    platforms = ["host"] + (["android"] if args.ndk else [])
    toolchain_options = {
        "host": [],
        "android": [
            f"-DCMAKE_TOOLCHAIN_FILE={args.ndk}/build/cmake/android.toolchain.cmake",
            "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-27",
        ],
    }
    optional_off = [f"-DANDSWAPCHAIN_BUILD_{name}=OFF" for name in
                    ("ANDROID", "GRAPHICS", "OVERLAY", "CRASH_RECORDER", "POINTER_ADAPTER")]
    status = "passed"
    failure = None
    device = None
    try:
        toolchain["host"] = platform.platform()
        toolchain["cmake"] = execute("cmake-version", ["cmake", "--version"])
        toolchain["host_cxx"] = execute("host-cxx-version", ["c++", "--version"])
        if args.ndk:
            toolchain["ndk_source_properties"] = (args.ndk / "source.properties").read_text().strip()
            toolchain["android_abi"] = "arm64-v8a"
            toolchain["android_api"] = 27
        if adb:
            execute("device-fixture-directory", [*adb, "shell", "mkdir", "-p", remote])
            device = {
                "serial_sha256": hashlib.sha256(args.device.encode()).hexdigest(),
                "model": execute("device-model", [*adb, "shell", "getprop", "ro.product.model"]),
                "api": execute("device-api", [*adb, "shell", "getprop", "ro.build.version.sdk"]),
                "scope": "Owned byte fixtures only; generated instructions are never executed.",
            }
        for target_platform in platforms:
            for config in ("Debug", "Release"):
                name = target_platform + "-assemble-" + config.lower()
                build = ROOT / "build" / name
                output = execute(name + "-configure", [
                    "cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
                    f"-DCMAKE_BUILD_TYPE={config}", "-DBUILD_TESTING=ON",
                    "-DANDSWAPCHAIN_BUILD_ASSEMBLE=ON", *optional_off,
                    f"-DANDSWAPCHAIN_ENABLE_SANITIZERS={'ON' if target_platform == 'host' and config == 'Debug' else 'OFF'}",
                    *toolchain_options[target_platform],
                ])
                verify_configuration(name, build, target_platform, "Assemble", output)
                execute(name + "-build", ["cmake", "--build", build, "--parallel", args.jobs,
                    "--target", *FIXTURES, "andswapchain_header_Assembler",
                    "andswapchain_header_Disassembler"])
                if target_platform == "host":
                    execute(name + "-ctest", ["ctest", "--test-dir", build,
                        "--output-on-failure", "--no-tests=error", "-R",
                        "^andswapchain_(assemble_contracts|assemble_roundtrip|assembler_roundtrip|disassembler_roundtrip)$"])
                    artifacts.update(build / fixture for fixture in FIXTURES)
                else:
                    for fixture in FIXTURES:
                        run_fixture(name + "-" + fixture, build / fixture, target_platform)
                artifacts.update(build / ("libAndSwapChainHook" + component + ".a")
                                 for component in ("Assembler", "Disassembler"))
                for engine in ("keystone", "capstone"):
                    engine_build = build / "_deps" / ("andswapchain_" + engine + "-build")
                    artifacts.update(engine_build.rglob("libandswapchain_" + engine + ".a"))
            for component in COMPONENTS:
                name = target_platform + "-" + component.lower()
                build = WORK / (name + "-source")
                prefix = WORK / (name + "-relocated-install")
                output = execute(name + "-source-configure", [
                    "cmake", "-S", ROOT / "tests/assemble_consumer", "-B", build,
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                    f"-DANDSWAPCHAIN_SOURCE_DIR={ROOT}", f"-DASSEMBLE_COMPONENT={component}",
                    f"-DCMAKE_INSTALL_PREFIX={WORK / (name + '-original-install')}",
                    *toolchain_options[target_platform],
                ])
                verify_configuration(name, build, target_platform, component, output)
                execute(name + "-source-build", ["cmake", "--build", build, "--parallel", args.jobs])
                run_fixture(name + "-source-consumer", build / "consumer", target_platform)
                execute(name + "-install", ["cmake", "--install", build, "--prefix", prefix])
                installed = WORK / (name + "-installed")
                execute(name + "-installed-configure", [
                    "cmake", "-S", ROOT / "tests/assemble_consumer", "-B", installed,
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                    f"-DASSEMBLE_COMPONENT={component}", f"-DCMAKE_PREFIX_PATH={prefix}",
                    f"-DCMAKE_FIND_ROOT_PATH={prefix}", *toolchain_options[target_platform],
                ])
                execute(name + "-installed-build", ["cmake", "--build", installed, "--parallel", args.jobs])
                run_fixture(name + "-installed-consumer", installed / "consumer", target_platform)
                artifacts.update(path for path in prefix.rglob("*") if path.is_file()
                                 and (path.suffix == ".a" and ("Assembler" in path.name or
                                      "Disassembler" in path.name or "keystone" in path.name or
                                      "capstone" in path.name) or path.suffix == ".cmake" or
                                      "licenses" in path.parts))
                for header in (("Assembler", "Disassembler") if component == "Assemble" else (component,)):
                    artifacts.add(prefix / "include/AndSwapChainHook" / (header + ".h"))
        reference = ROOT / "build/host-assemble-release/_deps"
        for engine in ("keystone", "capstone"):
            source = reference / ("andswapchain_" + engine + "-src")
            licenses = ROOT / "cmake/analysis/licenses" / engine
            for notice in sorted(licenses.rglob("*")):
                if notice.is_file():
                    relative = notice.relative_to(licenses)
                    upstream = source / ("LICENSES" if engine == "capstone" else "") / relative
                    if notice.read_bytes() != upstream.read_bytes():
                        raise RuntimeError("License content differs from upstream: " + str(relative))
            tree = hashlib.sha256()
            count = 0
            for path in sorted(source.rglob("*")):
                if path.is_file():
                    tree.update(str(path.relative_to(source)).encode() + b"\0")
                    tree.update(hashlib.sha256(path.read_bytes()).digest())
                    count += 1
            dependencies.append({"engine": engine, "source_tree_sha256": tree.hexdigest(),
                                 "source_tree_files": count,
                                 "tree_hash_format": "Sorted POSIX relative path, NUL, binary SHA-256 of each file.",
                                 "license_files_match_fetched_source": True})
        if [digest(path) for path in sorted(sources)] != source_hashes:
            raise RuntimeError("A language-tool source input changed during validation.")
    except (OSError, RuntimeError) as error:
        status = "failed"
        failure = str(error)
    manifest_path = ROOT / "tests/verification.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["assembly_validation"] = {
        "schema": 1,
        "status": status,
        "scope": "Assembler, Disassembler, and Assemble only. Other top-level component evidence retains its original source mapping.",
        "producer_configurations": [p + "-" + c for p in platforms for c in ("Debug", "Release")],
        "consumer_configuration": "Release",
        "consumer_components": list(COMPONENTS),
        "consumer_forms": ["add_subdirectory", "relocated find_package"],
        "sanitizers": "AddressSanitizer and UndefinedBehaviorSanitizer on host Debug wrappers and fixtures; upstream engines are not instrumented.",
        "toolchain": toolchain,
        "device": device,
        "checks": checks,
        "dependencies": dependencies,
        "dependency_configurations": dependency_configurations,
        "source_hashes": source_hashes,
        "artifacts": [digest(path) for path in sorted(artifacts) if path.is_file()],
        "limitations": [
            "Consumer builds use Release; producer fixtures cover Debug and Release.",
            "Generated AArch64 instruction bytes are not executed.",
            "No Dobby, hook, instrumentation, concealment, or process-permission validation is performed.",
            *([] if args.ndk else ["Android cross-builds were not requested."]),
            *([] if adb else ["Android device execution is unverified in this run."]),
        ],
    }
    if failure:
        manifest["assembly_validation"]["failure"] = failure
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    if failure:
        raise SystemExit(failure)


if __name__ == "__main__":
    main()
