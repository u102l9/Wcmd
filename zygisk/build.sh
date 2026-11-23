#!/bin/bash
# 本地编译 arm64-v8a.so (需 NDK r25+/r26, 已设 ANDROID_NDK_ROOT)
set -e
: "${ANDROID_NDK_ROOT:?请先 export ANDROID_NDK_ROOT=/path/to/android-ndk-rXX}"
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_NATIVE_API_LEVEL=29 \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cp build/arm64-v8a.so arm64-v8a.so
echo "done: zygisk/arm64-v8a.so"
