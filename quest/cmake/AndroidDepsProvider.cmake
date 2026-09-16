# Descent 3 - Meta Quest port
# Dependency provider for Android/Quest builds.
#
# vcpkg has no usable Quest triplet, so instead of editing the existing
# find_package() calls scattered through the tree we install a CMake dependency
# provider (CMake >= 3.24). It intercepts find_package() for the handful of
# packages vcpkg would normally supply and satisfies them from source via
# FetchContent. ZLIB is deliberately not intercepted: the NDK sysroot ships it.
#
# This keeps the Android port's diff against upstream limited to additive files.

include_guard(GLOBAL)
include(FetchContent)

set(D3_SDL3_TAG      "release-3.2.6"  CACHE STRING "SDL3 git tag")
set(D3_GLM_TAG       "1.0.1"          CACHE STRING "glm git tag")
set(D3_PLOG_TAG      "1.1.10"         CACHE STRING "plog git tag")
set(D3_HTTPLIB_TAG   "v0.23.0"        CACHE STRING "cpp-httplib git tag")

# --- SDL3 ---------------------------------------------------------------
# Quest still needs SDL for audio, threads, timers and the Android activity
# lifecycle. Video is initialised but the real presentation happens through
# OpenXR, so the renderer/window backends are trimmed where SDL allows.
set(SDL_SHARED   ON  CACHE BOOL "" FORCE)
set(SDL_STATIC   OFF CACHE BOOL "" FORCE)
set(SDL_TEST     OFF CACHE BOOL "" FORCE)
set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        ${D3_SDL3_TAG}
  GIT_SHALLOW    TRUE
)

# --- glm ----------------------------------------------------------------
set(GLM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        ${D3_GLM_TAG}
  GIT_SHALLOW    TRUE
)

# --- plog ---------------------------------------------------------------
set(PLOG_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(plog
  GIT_REPOSITORY https://github.com/SergiusTheBest/plog.git
  GIT_TAG        ${D3_PLOG_TAG}
  GIT_SHALLOW    TRUE
)

# --- cpp-httplib --------------------------------------------------------
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB    OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        ${D3_HTTPLIB_TAG}
  GIT_SHALLOW    TRUE
)

macro(d3_android_provide_dependency method dep_name)
  if("${dep_name}" STREQUAL "SDL3")
    FetchContent_MakeAvailable(SDL3)
    set(SDL3_FOUND TRUE)
  elseif("${dep_name}" STREQUAL "glm")
    FetchContent_MakeAvailable(glm)
    set(glm_FOUND TRUE)
  elseif("${dep_name}" STREQUAL "plog")
    FetchContent_MakeAvailable(plog)
    set(plog_FOUND TRUE)
  elseif("${dep_name}" STREQUAL "httplib")
    FetchContent_MakeAvailable(httplib)
    set(httplib_FOUND TRUE)
  endif()
endmacro()

cmake_language(
  SET_DEPENDENCY_PROVIDER d3_android_provide_dependency
  SUPPORTED_METHODS FIND_PACKAGE
)
