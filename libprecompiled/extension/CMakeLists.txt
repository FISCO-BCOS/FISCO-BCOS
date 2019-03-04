
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(extension ${SRC_LIST} ${HEADERS})
target_include_directories(extension PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(extension PRIVATE blockverifier ethcore)
target_link_libraries(extension PUBLIC storage devcore JsonCpp)
