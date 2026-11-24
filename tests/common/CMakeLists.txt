# Tests of the code plumsg and plumnin share (common/), built once in plumemu's build.
add_executable(plum_common_tests common_tests.cpp ${PLUM_COMMON_DIR}/src/frontend/library_names.cpp
               ${PLUM_COMMON_DIR}/src/frontend/game_info_data.cpp ${PLUM_COMMON_DIR}/src/frontend/texture_cache.cpp)
target_link_libraries(plum_common_tests PRIVATE plum_common)
set_target_properties(plum_common_tests PROPERTIES OUTPUT_NAME common_tests)
add_test(NAME common_tests COMMAND plum_common_tests)
