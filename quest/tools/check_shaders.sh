#!/usr/bin/env bash
# Compile and link the engine's shaders with real drivers (see shader_check.cpp).
#
#   check_shaders.sh           # both: headset GLES and host desktop GL
#   check_shaders.sh --device  # connected headset only (needs builds/quest)
#   check_shaders.sh --host    # build host only (configures builds/linux-check if needed)
set -euo pipefail
D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$D3_ROOT/quest/env.sh"
MODE="${1:---all}"
SRC="$D3_ROOT/quest/tools/shader_check.cpp"
rc=0

if [[ "$MODE" == "--all" || "$MODE" == "--device" ]]; then
  BUILD_DIR="${BUILD_DIR:-$D3_ROOT/builds/quest}"
  OUT="$BUILD_DIR/tools/shader_check"
  mkdir -p "$(dirname "$OUT")"
  cmake --build "$BUILD_DIR" --target renderer >/dev/null   # regenerates shaders.h
  "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" \
    --target=aarch64-linux-android29 -std=c++20 -O1 -DD3_GLES \
    -I "$D3_ROOT/renderer" -I "$BUILD_DIR/renderer/generated" \
    "$SRC" -static-libstdc++ -lEGL -lGLESv3 -o "$OUT"
  adb push "$OUT" /data/local/tmp/shader_check >/dev/null 2>&1
  adb shell chmod 755 /data/local/tmp/shader_check
  echo "=== device (GLES) ==="
  adb shell /data/local/tmp/shader_check | tr -d '\r' || rc=1
fi

if [[ "$MODE" == "--all" || "$MODE" == "--host" ]]; then
  HOST_BUILD="${HOST_BUILD_DIR:-$D3_ROOT/builds/linux-check}"
  OUT="$HOST_BUILD/tools/shader_check"
  mkdir -p "$(dirname "$OUT")"
  if [[ ! -f "$HOST_BUILD/build.ninja" ]]; then
    # A plain desktop configure; deps come from the same provider as the Quest build.
    cmake -S "$D3_ROOT" -B "$HOST_BUILD" -G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release \
      -DUSE_VCPKG=OFF -DBUILD_TESTING=OFF \
      -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$D3_ROOT/quest/cmake/AndroidDepsProvider.cmake" >/dev/null
  fi
  cmake --build "$HOST_BUILD" --target renderer SDL3-shared >/dev/null
  c++ -std=c++20 -O1 \
    -I "$D3_ROOT/renderer" -I "$HOST_BUILD/renderer/generated" \
    -I "$HOST_BUILD/_deps/sdl3-src/include" \
    "$SRC" -L "$HOST_BUILD/_deps/sdl3-build" -Wl,-rpath,"$HOST_BUILD/_deps/sdl3-build" \
    -lSDL3 -lGL -o "$OUT"
  echo "=== host (desktop GL) ==="
  "$OUT" || rc=1
fi
exit $rc
