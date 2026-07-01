cmake_minimum_required(VERSION 3.22.1)

if(DEFINED ENV{NDK_HOME})
    set(ANDROID_PLATFORM 27)
    set(ANDROID_ABI arm64-v8a)
    file(TO_CMAKE_PATH "$ENV{NDK_HOME}" NDK_HOME_CMAKE)
    set(CMAKE_TOOLCHAIN_FILE "${NDK_HOME_CMAKE}/build/cmake/android.toolchain.cmake")
else()
    message(FATAL_ERROR "NDK_HOME environment variable not set. Please set it to the path of your Android NDK.")
endif()

project(AndSwapChainHook C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fvisibility=hidden -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections -Wl,--strip-all -Wl,--exclude-libs,ALL)
endif()

file(GLOB_RECURSE COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.c
)
file(GLOB_RECURSE ASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.s
)

file(GLOB IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_android.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_opengl3.cpp
)

option(USE_KITTYMEMORYEX "Use KittyMemoryEx backend (pvm syscall) instead of KittyMemory (mprotect+memcpy)" ON)

if(USE_KITTYMEMORYEX)
    file(GLOB KITTY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/external/KittyMemoryEx/KittyMemoryEx/*.cpp)
    set(KITTY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/KittyMemoryEx)
else()
    file(GLOB KITTY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/external/KittyMemory/KittyMemory/*.cpp)
    set(KITTY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/KittyMemory)
endif()

set(LIBRARY_SOURCES
    ${COMMON_SOURCES}
    ${IMGUI_SOURCES}
    ${KITTY_SOURCES}
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${KITTY_INCLUDE_DIR}
)

set(LIBS
    android
    log
    vulkan
    GLESv3
    EGL
    ${CMAKE_CURRENT_SOURCE_DIR}/external/Dobby/arm64-v8a/libdobby.a
)

add_library(${PROJECT_NAME}_asm OBJECT ${ASM_SOURCES})
set_target_properties(${PROJECT_NAME}_asm PROPERTIES EXPORT_COMPILE_COMMANDS OFF)

add_library(${PROJECT_NAME} SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Library.cpp
    ${LIBRARY_SOURCES}
    $<TARGET_OBJECTS:${PROJECT_NAME}_asm>
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${USE_KITTYMEMORYEX}>:kUSE_KITTYMEMORYEX>
    kNO_KEYSTONE
    kUSE_LOGCAT # KITTY_LOGX
    kANDROID_LOG
    kANDROID_LOG_TAG="INJECT"
    kPROJECT_NAME="${PROJECT_NAME}"
)

target_link_libraries(${PROJECT_NAME} ${LIBS})
