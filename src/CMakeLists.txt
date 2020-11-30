add_executable(root-throughput root_throughput.cxx)
target_link_libraries(root-throughput ${ROOT_LIBRARIES})
target_include_directories(root-throughput PUBLIC ${ROOT_INCLUDE_DIRS})
