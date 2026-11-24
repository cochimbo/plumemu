cmake_minimum_required(VERSION 3.16)
# plumemu: plumsg (Sega, in sega/) and plumnin (Nintendo, in nintendo/) built together.
# Each one is still its own CMake project with its own targets and options and can be
# built alone (cmake -S sega); this file adds them side by side, after the code they
# share (common/), so both programs, their headless runners and all their tests come out
# of one build folder:
#   build/sega/plumsg, build/sega/plumsg-headless, build/sega/core_tests...
#   build/nintendo/plumnin, build/nintendo/plumnin-headless, build/nintendo/core_tests...
#   build/tests/common/common_tests (the shared code's)
project(plumemu LANGUAGES C CXX)

option(PLUM_BUILD_SEGA "Build plumsg (Mega Drive, 32X, Mega CD, Master System, Game Gear)" ON)
option(PLUM_BUILD_NINTENDO "Build plumnin (Game Boy, Game Boy Color, NES, Super Nintendo)" ON)
option(PLUM_FETCH_SDL2 "Download and build SDL2 if it is not installed" ON)
option(PLUM_BUILD_TESTS "Build the test programs" ON)
# Off builds only the cores, the headless runners and the tests: no SDL2, ImGui or OpenGL.
option(PLUM_BUILD_FRONTEND "Build the emulators with their window (needs SDL2 and OpenGL)" ON)

# The options above set each program's own (PLUMSG_*, PLUMNIN_*), unless those are given.
foreach(opt FETCH_SDL2 BUILD_TESTS BUILD_FRONTEND)
  foreach(app PLUMSG PLUMNIN)
    if(NOT DEFINED ${app}_${opt})
      set(${app}_${opt} ${PLUM_${opt}} CACHE BOOL "Set from PLUM_${opt}")
    endif()
  endforeach()
endforeach()

if(PLUM_BUILD_TESTS)
  enable_testing()
endif()
# The code both share (namespace plum): plum_common, and the sources each program lists.
add_subdirectory(common)
if(PLUM_BUILD_SEGA)
  add_subdirectory(sega)
endif()
if(PLUM_BUILD_NINTENDO)
  add_subdirectory(nintendo)
endif()
# The tests of the shared code (each program's own are in sega/tests and nintendo/tests).
if(PLUM_BUILD_TESTS)
  set(PLUM_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
  add_subdirectory(tests/common)
endif()
