# Get doctest
message(STATUS "Setting up doctest testing framework")
include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/onqtam/doctest.git
  GIT_TAG        2.4.6)
FetchContent_Populate(doctest)
if (doctest_POPULATED)
   message(STATUS "Setting up doctest testing framework - done")
else()
   message(STATUS "Setting up doctest testing framework - ERROR")
endif()
add_subdirectory(${doctest_SOURCE_DIR} ${doctest_BINARY_DIR})
