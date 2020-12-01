cmake_minimum_required(VERSION 3.11)
project (root-readspeed CXX)
add_compile_options(-Wall -Wextra -Wpedantic)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
   message(STATUS "Build type not specified, defaulting to ${CMAKE_BUILD_TYPE}")
endif()

find_package(ROOT REQUIRED COMPONENTS Tree)

add_subdirectory(src)
