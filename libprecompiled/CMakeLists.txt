
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")
set(WEDPR_LIB_DIR "${CMAKE_SOURCE_DIR}/deps/lib/libffi_hidden_asset.so")

if(NOT WEDPR_EXTENSION)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/extension/WedprPrecompiled.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS})
    set(REMOVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extension/WedprPrecompiled.cpp)
    list(REMOVE_ITEM SRC_LIST ${REMOVE_SOURCES})

    set(REMOVE_HEADERS_WEDPR_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/extension/wedpr-generated/asset_hiding/*.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS_WEDPR_GENERATED})
endif()

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore)

if(WEDPR_EXTENSION)
    target_link_libraries(precompiled PUBLIC storage devcore JsonCpp ${WEDPR_LIB_DIR})
else()
    target_link_libraries(precompiled PUBLIC storage devcore JsonCpp)
endif()

add_subdirectory(extension)
