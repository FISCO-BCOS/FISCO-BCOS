
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")
add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore)
target_link_libraries(precompiled PUBLIC storage utilities JsonCpp)
target_link_libraries(precompiled PRIVATE Paillier GroupSig VRF)
add_subdirectory(extension)
