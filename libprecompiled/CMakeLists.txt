
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")
add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier protocol )
target_link_libraries(precompiled PUBLIC utilities JsonCpp storage)
target_link_libraries(precompiled PRIVATE Paillier GroupSig VRF)
add_subdirectory(extension)
