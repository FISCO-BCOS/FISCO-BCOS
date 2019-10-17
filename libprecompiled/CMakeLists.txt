
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")
set(WEDPR_HIDDEN_ASSET_DIR "/Users/asher/WeDPR/target/debug/libffi_hidden_asset.dylib")
set(WEDPR_ANONYMOUS_VOTING_DIR "/Users/asher/WeDPR/target/debug/libffi_anonymous_voting.dylib")

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
  # target_link_libraries(precompiled PUBLIC storage devcore JsonCpp "/mnt/d/dev/rust/rust_learning/WeDPR/target/release/libffi_hidden_asset.a")
else()
    target_link_libraries(precompiled PUBLIC storage devcore JsonCpp)
endif()

add_subdirectory(extension)
