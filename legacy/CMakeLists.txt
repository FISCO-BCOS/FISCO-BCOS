# Legacy code include path — provides access to legacy headers under legacy/
add_library(bcos-legacy INTERFACE)
target_include_directories(bcos-legacy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
