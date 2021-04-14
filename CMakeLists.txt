cmake_minimum_required(VERSION 3.11)
project (root-readspeed CXX)
add_compile_options(-Wall -Wextra -Wpedantic)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release/RelWithDebInfo/Debug)." FORCE)
   message(STATUS "Build type not specified, defaulting to ${CMAKE_BUILD_TYPE}.")
endif()
option(ROOTREADSPEED_TESTS "Set to ON to build unit tests." OFF)

message(STATUS "Looking for ROOT")
find_package(ROOT REQUIRED COMPONENTS Tree)
message(STATUS "ROOT ${ROOT_VERSION} found at ${ROOT_BINDIR}")

add_subdirectory(src)

if (ROOTREADSPEED_TESTS)
   add_subdirectory(test)
endif()
