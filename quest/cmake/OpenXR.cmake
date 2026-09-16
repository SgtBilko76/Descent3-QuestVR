# Descent 3 - Meta Quest port
# OpenXR loader for Android.
#
# Uses the official Khronos loader AAR from Maven Central, which ships the
# arm64 libopenxr_loader.so and the matching headers. The .so must also be
# packaged into the APK (quest/build_apk.sh picks it up from D3_OPENXR_LOADER).

include_guard(GLOBAL)
include(FetchContent)

set(D3_OPENXR_VERSION "1.1.63" CACHE STRING "OpenXR loader version")
set(D3_OPENXR_SHA256 "622419d2f6741c3443a3beb4779af0764318edd01830de967f24c741ebcded73"
    CACHE STRING "SHA-256 of the OpenXR loader AAR")

FetchContent_Declare(openxr_loader_aar
  URL "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/${D3_OPENXR_VERSION}/openxr_loader_for_android-${D3_OPENXR_VERSION}.aar"
  URL_HASH SHA256=${D3_OPENXR_SHA256}
  # An .aar is a zip; name it so CMake knows how to extract it.
  DOWNLOAD_NAME openxr_loader_for_android.zip
)
FetchContent_MakeAvailable(openxr_loader_aar)

set(_aar "${openxr_loader_aar_SOURCE_DIR}")
set(D3_OPENXR_LOADER "${_aar}/prefab/modules/openxr_loader/libs/android.${ANDROID_ABI}/libopenxr_loader.so"
    CACHE INTERNAL "OpenXR loader shared library to package")
if(NOT EXISTS "${D3_OPENXR_LOADER}")
  message(FATAL_ERROR "OpenXR loader for ${ANDROID_ABI} not found in ${_aar}")
endif()

add_library(OpenXR::openxr_loader SHARED IMPORTED GLOBAL)
set_target_properties(OpenXR::openxr_loader PROPERTIES
  IMPORTED_LOCATION "${D3_OPENXR_LOADER}"
  IMPORTED_NO_SONAME OFF
  INTERFACE_INCLUDE_DIRECTORIES "${_aar}/prefab/modules/headers/include"
)
