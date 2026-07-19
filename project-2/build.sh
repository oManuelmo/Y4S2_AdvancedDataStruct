mkdir -p build
cd build

# Configure with options
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_CRAWLER=ON \
    -DBUILD_BENCHMARK=ON

# Build
cmake --build . --config Release -j$(nproc)