# Keystone consumes checked-in target tables and does not need a native LLVM build.
foreach(relative CMakeLists.txt llvm/CMakeLists.txt)
    file(READ "${SOURCE_DIR}/${relative}" content)
    string(REPLACE "cmake_policy(SET CMP0051 OLD)" "cmake_policy(SET CMP0051 NEW)" content "${content}")
    string(REPLACE "set(LLVM_USE_HOST_TOOLS ON)" "set(LLVM_USE_HOST_TOOLS OFF)" content "${content}")
    string(FIND "${content}" "# Modified by AndSwapChainHook:" notice)
    if(notice EQUAL -1)
        string(PREPEND content "# Modified by AndSwapChainHook: checked-in target tables and current CMake policies.\n")
    endif()
    file(WRITE "${SOURCE_DIR}/${relative}" "${content}")
endforeach()

file(READ "${SOURCE_DIR}/CMakeLists.txt" content)
string(FIND "${content}" "if(NOT BUILD_LIBS_ONLY)\n  add_subdirectory(suite/fuzz)\nendif()" library_only_guard)
if(library_only_guard EQUAL -1)
    string(REPLACE "add_subdirectory(suite/fuzz)" "if(NOT BUILD_LIBS_ONLY)\n  add_subdirectory(suite/fuzz)\nendif()" content "${content}")
endif()
file(WRITE "${SOURCE_DIR}/CMakeLists.txt" "${content}")

# Android AArch64 libraries have an Android host triple in LLVM's generated configuration.
set(host_triple_file "${SOURCE_DIR}/llvm/cmake/modules/GetHostTriple.cmake")
file(READ "${host_triple_file}" content)
string(REPLACE "elseif( (MINGW OR ANDROID) AND NOT MSYS )"
    "elseif( ANDROID )\n    set( value \"aarch64-linux-android\" )\n  elseif( MINGW AND NOT MSYS )"
    content "${content}")
string(REPLACE "Target triple:" "Keystone host triple:" content "${content}")
string(FIND "${content}" "# Modified by AndSwapChainHook:" notice)
if(notice EQUAL -1)
    string(PREPEND content "# Modified by AndSwapChainHook: Android AArch64 host identification (2026-09-12).\n")
endif()
file(WRITE "${host_triple_file}" "${content}")
