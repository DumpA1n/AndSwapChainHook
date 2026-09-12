prefix="${RUNNER_TEMP}/andswapchain-android-debug"
cmake --install build/android-debug --prefix "$prefix"
options=(-G Ninja '-DCMAKE_BUILD_TYPE=Debug')
options+=("-DCMAKE_TOOLCHAIN_FILE=${ANDROID_HOME}/ndk/29.0.14206865/build/cmake/android.toolchain.cmake")
options+=(-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-27)
cmake -S tests/consumer -B build/ci-android-installed "${options[@]}" \
  -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_FIND_ROOT_PATH="$prefix"
cmake --build build/ci-android-installed --parallel 4
cmake -S tests/consumer -B build/ci-android-source "${options[@]}" \
  -DANDSWAPCHAIN_SOURCE_DIR="${GITHUB_WORKSPACE}"
cmake --build build/ci-android-source --parallel 4

