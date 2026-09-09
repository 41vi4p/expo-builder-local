# Only pulls in the pure-logic units under test (json.cpp, detect.cpp) - not linked
# against the main `ebl` target at all, so this never needs libcurl/OpenSSL/Threads
# and stays fast to build. Add a source here as more of src/ becomes unit-testable.
add_executable(ebl_tests
  test_main.cpp
  test_json.cpp
  test_detect.cpp
  test_version_compare.cpp
  test_docker_stats.cpp
  ../src/json.cpp
  ../src/detect.cpp
  ../src/version_compare.cpp
  ../src/docker_stats.cpp
)
target_include_directories(ebl_tests PRIVATE ../src)
if(NOT MSVC)
  target_compile_options(ebl_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME ebl_tests COMMAND ebl_tests)
