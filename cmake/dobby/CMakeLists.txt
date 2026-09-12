cmake_minimum_required(VERSION 3.22.1)
project(AndSwapChainDobby VERSION 1.0.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The Dobby hook-only facade supports Android AArch64 only.")
endif()
include(FetchContent)
find_program(PATCH_EXECUTABLE patch REQUIRED)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(andswapchain_dobby_source
    URL https://codeload.github.com/jmpews/Dobby/tar.gz/5dfc8546954ce3b3198132ab13fddb89ee92cdd7
    URL_HASH SHA256=01a417233c40929aa21098c8a9724c00f6f05b5cde3f11abcac84b8e00053748
    SOURCE_SUBDIR andswapchain-no-upstream-build
    PATCH_COMMAND "${PATCH_EXECUTABLE}" -p1 -i "${CMAKE_CURRENT_SOURCE_DIR}/ownership.patch")
FetchContent_MakeAvailable(andswapchain_dobby_source)
set(dobby_source "${andswapchain_dobby_source_SOURCE_DIR}")
add_library(AndSwapChainDobby STATIC
    Entry.cpp Memory.cpp
    "${dobby_source}/source/InstructionRelocation/arm64/InstructionRelocationARM64.cc"
    "${dobby_source}/source/TrampolineBridge/Trampoline/trampoline_arm64.cc"
    "${dobby_source}/source/Backend/UserMode/UnifiedInterface/platform-posix.cc")
add_library(Dobby::Dobby ALIAS AndSwapChainDobby)
set_target_properties(AndSwapChainDobby PROPERTIES
    EXPORT_NAME Dobby OUTPUT_NAME andswapchain_dobby POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF ANDSWAPCHAIN_DOBBY_CONTRACT_VERSION 1
    EXPORT_PROPERTIES ANDSWAPCHAIN_DOBBY_CONTRACT_VERSION)
target_compile_features(AndSwapChainDobby PRIVATE cxx_std_17)
target_compile_definitions(AndSwapChainDobby PRIVATE DOBBY_LOGGING_DISABLE)
target_compile_options(AndSwapChainDobby PRIVATE -g -fno-omit-frame-pointer
    "-ffile-prefix-map=${dobby_source}=dobby-upstream"
    "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=andswapchain-dobby"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build"
    -fdebug-compilation-dir=.)
target_include_directories(AndSwapChainDobby PRIVATE
    "${dobby_source}" "${dobby_source}/include" "${dobby_source}/source"
    "${dobby_source}/source/dobby" "${dobby_source}/external"
    "${dobby_source}/external/logging" "${dobby_source}/builtin-plugin"
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(AndSwapChainDobby PRIVATE log)
install(TARGETS AndSwapChainDobby EXPORT DobbyTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/dobby.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE ownership.patch DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/AndSwapChainDobby)
install(EXPORT DobbyTargets NAMESPACE Dobby:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dobby)
configure_package_config_file(DobbyConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/DobbyConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dobby)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/DobbyConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/DobbyConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/DobbyConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dobby)
