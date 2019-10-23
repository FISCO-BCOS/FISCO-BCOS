
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")
if (APPLE)
    set(WEDPR_HIDDEN_ASSET_DIR "${CMAKE_SOURCE_DIR}/deps/lib/libffi_hidden_asset.dylib")
    set(WEDPR_ANONYMOUS_VOTING_DIR "${CMAKE_SOURCE_DIR}/deps/lib/libffi_anonymous_voting.dylib")
else()
    set(WEDPR_HIDDEN_ASSET_DIR "${CMAKE_SOURCE_DIR}/deps/lib/libffi_hidden_asset.so")
    set(WEDPR_ANONYMOUS_VOTING_DIR "${CMAKE_SOURCE_DIR}/deps/lib/libffi_anonymous_voting.so")
endif()

if(NOT WEDPR_EXTENSION)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/extension/WedprPrecompiled.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS})
    set(REMOVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extension/WedprPrecompiled.cpp)
    list(REMOVE_ITEM SRC_LIST ${REMOVE_SOURCES})

    set(REMOVE_HEADERS_WEDPR_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/extension/wedpr-generated/*.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS_WEDPR_GENERATED})
endif()

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore pthread m)

if(WEDPR_EXTENSION)
    target_link_libraries(precompiled PUBLIC storage devcore JsonCpp ${WEDPR_HIDDEN_ASSET_DIR} ${WEDPR_ANONYMOUS_VOTING_DIR})
else()
    target_link_libraries(precompiled PUBLIC storage devcore JsonCpp)
endif()

add_subdirectory(extension)
