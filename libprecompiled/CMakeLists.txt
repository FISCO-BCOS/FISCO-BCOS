
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore)
target_link_libraries(precompiled PUBLIC storage devcore JsonCpp)

add_subdirectory(extension)
