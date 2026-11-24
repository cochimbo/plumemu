# The code plumsg (sega/) and plumnin (nintendo/) share, in namespace plum.
#
# plum_common: what needs no SDL (texts and translations, rewind, recordings). Both
# programs link it through their cores, so in plumemu's build it is built once.
#
# The common frontend and render files (common/src/frontend, common/src/render) need
# SDL2, OpenGL and ImGui, which each program sets up for itself, so each program still
# lists them among its own sources: PLUM_COMMON_DIR/src/frontend/audio.cpp...
#
# Added by the root CMakeLists.txt, or by sega/ or nintendo/ when either is built alone.
cmake_minimum_required(VERSION 3.16)
project(plum_common LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

find_package(Threads REQUIRED)

add_library(plum_common STATIC
  src/core/i18n.cpp
  src/core/recording.cpp
  src/core/rewind.cpp
)
# Its headers are included as "core/i18n.h", "frontend/audio.h", "render/gl.h"...
target_include_directories(plum_common PUBLIC src)
# stb_image_write (public domain / MIT, vendored) encodes the JPEG frames of the AVI recordings.
target_include_directories(plum_common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb)
target_link_libraries(plum_common PUBLIC Threads::Threads)
