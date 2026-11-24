# The code plumsg (sega/) and plumnin (nintendo/) share, in namespace plum.
#
# plum_common: what needs no SDL (the Console base with its savestates, texts and
# translations, rewind, recordings, the split of a frame into the 3D view's planes). Both
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

# miniz (vendored, MIT): zip files, savestate compression and PNG screenshots.
add_library(plum_miniz STATIC ../third_party/miniz/miniz.c)
set_target_properties(plum_miniz PROPERTIES LINKER_LANGUAGE C)
target_include_directories(plum_miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniz)
target_compile_definitions(plum_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
if(MSVC)
  target_compile_options(plum_miniz PRIVATE /W0)
else()
  target_compile_options(plum_miniz PRIVATE -w)
endif()

add_library(plum_common STATIC
  src/core/console_base.cpp
  src/core/i18n.cpp
  src/core/recording.cpp
  src/core/rewind.cpp
  src/render/planes.cpp
)
# Its headers are included as "core/i18n.h", "frontend/audio.h", "render/gl.h"...
target_include_directories(plum_common PUBLIC src)
# stb_image_write (public domain / MIT, vendored) encodes the JPEG frames of the AVI recordings.
target_include_directories(plum_common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb)
target_link_libraries(plum_common PUBLIC plum_miniz Threads::Threads)
