add_library(ReadSpeed INTERFACE)
add_library(ReadSpeed::ReadSpeed ALIAS ReadSpeed)
target_sources(ReadSpeed INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/ReadSpeed.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/ReadSpeedInput.cxx
)
target_link_libraries(ReadSpeed INTERFACE ROOT::RIO ROOT::Tree ROOT::TreePlayer ROOT::ROOTDataFrame)
target_include_directories(ReadSpeed INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(root-readspeed root_readspeed.cxx)
target_link_libraries(root-readspeed ReadSpeed::ReadSpeed)
