#!/usr/bin/env bash
# One-shot Quest build: host tools -> Android configure -> engine -> APK.
#
#   quest/build.sh                 # Release build + APK
#   quest/build.sh --install       # ...and adb install it
#   BUILD_TYPE=Debug quest/build.sh
set -euo pipefail

D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$D3_ROOT/quest/env.sh"

BUILD_TYPE="${BUILD_TYPE:-Release}"
HOST_DIR="$D3_ROOT/builds/quest-hosttools"
export BUILD_DIR="${BUILD_DIR:-$D3_ROOT/builds/quest}"

echo "==> host tools"
cmake -S "$D3_ROOT/quest/hosttools" -B "$HOST_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$HOST_DIR"

echo "==> configure (arm64-v8a, $BUILD_TYPE)"
cmake -S "$D3_ROOT" -B "$BUILD_DIR" -G Ninja -Wno-dev \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DUSE_VCPKG=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$D3_ROOT/quest/cmake/AndroidDepsProvider.cmake" \
  -DHogMaker_DIR="$HOST_DIR" >/dev/null

echo "==> build"
cmake --build "$BUILD_DIR"

"$D3_ROOT/quest/build_apk.sh"

if [[ "${1:-}" == "--install" ]]; then
  adb install -r "$BUILD_DIR/apk/descent3-quest.apk"
fi
