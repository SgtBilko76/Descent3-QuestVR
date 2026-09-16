#!/usr/bin/env bash
# Build a sideloadable Descent 3 APK for Meta Quest from an existing
# CMake Android build tree.
#
# Deliberately Gradle-free: the whole APK is aapt2 + javac + d8 + apksigner.
# That keeps the toolchain to what the Android SDK already ships and makes
# every step inspectable, which matters while the port is still moving.
#
# Environment (all optional):
#   APK_MODE=vr|flat          immersive VR app (default) or 2D panel app
#   APK_DEBUGGABLE=1|0        debuggable dev build (default) or release build
#   APK_VERSION_NAME, APK_VERSION_CODE
#   KEYSTORE, KEY_ALIAS, KEYSTORE_PASS, KEY_PASS
#                             signing key; defaults to a local debug key that
#                             is generated on first use
#   APK_NAME                  output file name (default descent3-quest.apk)
set -euo pipefail

D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$D3_ROOT/quest/env.sh"
BUILD_DIR="${BUILD_DIR:-$D3_ROOT/builds/quest}"
OUT_DIR="${OUT_DIR:-$BUILD_DIR/apk}"
PKG="com.descentdevelopers.descent3"

: "${ANDROID_SDK_ROOT:?set ANDROID_SDK_ROOT}"
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}"
BUILD_TOOLS="${BUILD_TOOLS:-$ANDROID_SDK_ROOT/build-tools/36.0.0}"
PLATFORM_JAR="${PLATFORM_JAR:-$ANDROID_SDK_ROOT/platforms/android-35/android.jar}"

AAPT2="$BUILD_TOOLS/aapt2"
D8="$BUILD_TOOLS/d8"
ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"

SDL_SRC="$BUILD_DIR/_deps/sdl3-src"
SDL_JAVA="$SDL_SRC/android-project/app/src/main/java"
LIBMAIN="$BUILD_DIR/build/libmain.so"
LIBSDL="$BUILD_DIR/_deps/sdl3-build/libSDL3.so"

for f in "$AAPT2" "$D8" "$ZIPALIGN" "$APKSIGNER" "$PLATFORM_JAR" "$LIBMAIN" "$LIBSDL" "$SDL_JAVA"; do
  [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"/{flat,gen,classes,dex,lib/arm64-v8a,assets}
OUT_DIR="$(cd "$OUT_DIR" && pwd)"  # later steps cd around

# vr (default): immersive OpenXR app. flat: 2D panel app without VR.
APK_MODE="${APK_MODE:-vr}"
MANIFEST="$OUT_DIR/AndroidManifest.xml"
case "$APK_MODE" in
  vr)   sed 's/D3VR_ENABLED_VALUE/true/' "$D3_ROOT/quest/app/AndroidManifest.xml" > "$MANIFEST" ;;
  flat) sed -e '/<!-- VR-BEGIN -->/,/<!-- VR-END -->/d' -e 's/D3VR_ENABLED_VALUE/false/' \
            "$D3_ROOT/quest/app/AndroidManifest.xml" > "$MANIFEST" ;;
  *)    echo "APK_MODE must be vr or flat" >&2; exit 1 ;;
esac

# The OpenXR loader libmain.so links against (fetched by quest/cmake/OpenXR.cmake).
OPENXR_LOADER="$(sed -n 's/^D3_OPENXR_LOADER:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"

echo "==> aapt2 compile"
"$AAPT2" compile --dir "$D3_ROOT/quest/app/res" -o "$OUT_DIR/flat/res.zip"

# Sideload dev builds are debuggable: this enables launch arguments
# (D3Activity) and "adb shell run-as". Set APK_DEBUGGABLE=0 for releases.
APK_DEBUGGABLE="${APK_DEBUGGABLE:-1}"
debug_flag=()
[[ "$APK_DEBUGGABLE" == 1 ]] && debug_flag=(--debug-mode)
APK_VERSION_NAME="${APK_VERSION_NAME:-dev}"
APK_VERSION_CODE="${APK_VERSION_CODE:-1}"
APK_NAME="${APK_NAME:-descent3-quest.apk}"

echo "==> aapt2 link"
"$AAPT2" link ${debug_flag[@]+"${debug_flag[@]}"} \
  -I "$PLATFORM_JAR" \
  --manifest "$MANIFEST" \
  --java "$OUT_DIR/gen" \
  --min-sdk-version 29 --target-sdk-version 32 \
  --version-code "$APK_VERSION_CODE" --version-name "$APK_VERSION_NAME" --replace-version \
  -o "$OUT_DIR/base.apk" \
  "$OUT_DIR/flat/res.zip"

echo "==> javac (SDL + D3Activity + generated R)"
find "$SDL_JAVA" "$D3_ROOT/quest/app/java" -name '*.java' > "$OUT_DIR/sources.txt"
find "$OUT_DIR/gen" -name '*.java' >> "$OUT_DIR/sources.txt"
javac -nowarn -encoding UTF-8 --release 11 \
  -classpath "$PLATFORM_JAR" \
  -d "$OUT_DIR/classes" \
  @"$OUT_DIR/sources.txt"

echo "==> d8"
find "$OUT_DIR/classes" -name '*.class' > "$OUT_DIR/classes.txt"
"$D8" --min-api 29 --lib "$PLATFORM_JAR" --output "$OUT_DIR/dex" @"$OUT_DIR/classes.txt"

echo "==> stage native libs"
cp "$LIBSDL"  "$OUT_DIR/lib/arm64-v8a/libSDL3.so"
cp "$LIBMAIN" "$OUT_DIR/lib/arm64-v8a/libmain.so"
if [[ -n "$OPENXR_LOADER" ]]; then
  cp "$OPENXR_LOADER" "$OUT_DIR/lib/arm64-v8a/libopenxr_loader.so"
fi
# Strip: the unstripped engine .so is ~47MB, almost all debug_info.
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
  "$OUT_DIR/lib/arm64-v8a/libmain.so" "$OUT_DIR/lib/arm64-v8a/libSDL3.so"

echo "==> assemble"
cp "$OUT_DIR/base.apk" "$OUT_DIR/unsigned.apk"
( cd "$OUT_DIR/dex" && zip -q "$OUT_DIR/unsigned.apk" classes.dex )
# Native libs are stored uncompressed and page-aligned so the loader can mmap
# them straight out of the APK (extractNativeLibs=false behaviour).
( cd "$OUT_DIR" && zip -q -r -0 unsigned.apk lib )

DEBUG_KEYSTORE="$D3_ROOT/quest/debug.keystore"
KEYSTORE="${KEYSTORE:-$DEBUG_KEYSTORE}"
KEY_ALIAS="${KEY_ALIAS:-androiddebugkey}"
export KEYSTORE_PASS="${KEYSTORE_PASS:-android}"
export KEY_PASS="${KEY_PASS:-$KEYSTORE_PASS}"
if [ ! -f "$KEYSTORE" ]; then
  if [[ "$KEYSTORE" != "$DEBUG_KEYSTORE" ]]; then
    echo "keystore not found: $KEYSTORE" >&2
    exit 1
  fi
  echo "==> generating debug keystore"
  keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Descent3 Quest Debug, OU=, O=, L=, S=, C=" >/dev/null 2>&1
fi

echo "==> zipalign + sign"
"$ZIPALIGN" -f -p 4 "$OUT_DIR/unsigned.apk" "$OUT_DIR/$APK_NAME"
"$APKSIGNER" sign --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
  --ks-pass env:KEYSTORE_PASS --key-pass env:KEY_PASS \
  --min-sdk-version 29 "$OUT_DIR/$APK_NAME"

echo
echo "APK ($APK_MODE, $APK_VERSION_NAME): $OUT_DIR/$APK_NAME  ($(du -h "$OUT_DIR/$APK_NAME" | cut -f1))"
