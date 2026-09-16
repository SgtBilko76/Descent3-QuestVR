# Descent 3 Quest port - build environment. Source this file.
# Every value can be overridden from the calling environment.
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_SDK_ROOT/ndk/28.2.13676358}"
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
export JAVA_HOME="${JAVA_HOME:-$HOME/android-studio/jbr}"
D3_SDK_CMAKE_BIN="${D3_SDK_CMAKE_BIN:-$ANDROID_SDK_ROOT/cmake/3.31.6/bin}"
export PATH="$D3_SDK_CMAKE_BIN:$JAVA_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$PATH"
