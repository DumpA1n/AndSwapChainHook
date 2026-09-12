cmake --install build/host-release --prefix "${RUNNER_TEMP}/andswapchain"
cmake -S tests/consumer -B build/ci-installed-consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="${RUNNER_TEMP}/andswapchain"
cmake --build build/ci-installed-consumer --parallel 4
build/ci-installed-consumer/consumer
cmake -S tests/consumer -B build/ci-source-consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DANDSWAPCHAIN_SOURCE_DIR="${GITHUB_WORKSPACE}"
cmake --build build/ci-source-consumer --parallel 4
build/ci-source-consumer/consumer

