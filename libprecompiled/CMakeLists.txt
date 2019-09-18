
file(GLOB_RECURSE SRC_LIST "*.cpp" "*.cc")
file(GLOB_RECURSE HEADERS "*.h")

if (NOT WEDPR_EXTENSION)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/extension/WedprPrecompiled.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS})
    set(REMOVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extension/WedprPrecompiled.cpp)
    list(REMOVE_ITEM SRC_LIST ${REMOVE_SOURCES})
endif()

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore pthread m)
target_link_libraries(precompiled PUBLIC storage devcore JsonCpp ProtoBuf)

add_subdirectory(extension)
