set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(blockverifier ${SRC_LIST} ${HEADERS})

target_include_directories(blockverifier PRIVATE ..)
target_link_libraries(blockverifier INTERFACE executivecontext evmc)
target_link_libraries(blockverifier PUBLIC storage)
