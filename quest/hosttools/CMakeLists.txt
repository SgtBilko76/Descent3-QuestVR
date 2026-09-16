# Host-side tools needed when cross-compiling for Android. The main project
# runs HogMaker at build time, so it must be built for the build machine and
# found via find_package(HogMaker) (see the CMAKE_CROSSCOMPILING branch in the
# root CMakeLists.txt).
cmake_minimum_required(VERSION 3.20)
project(D3HostTools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(D3_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(HogMaker
  "${D3_SOURCE_DIR}/tools/HogMaker/HogFormat.cpp"
  "${D3_SOURCE_DIR}/tools/HogMaker/HogMaker.cpp"
)
target_include_directories(HogMaker PRIVATE "${D3_SOURCE_DIR}/lib" "${D3_SOURCE_DIR}/tools/HogMaker")
export(TARGETS HogMaker FILE "${CMAKE_BINARY_DIR}/HogMakerConfig.cmake")
