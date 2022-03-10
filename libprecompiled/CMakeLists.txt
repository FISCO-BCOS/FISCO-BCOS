
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

if(APPLE)
    file(GLOB SIG_HEADERS "extension/group_ring_sig/*.h")
    file(GLOB SIG_SRCS "extension/group_ring_sig/*.cpp")

    list(REMOVE_ITEM SRC_LIST ${SIG_SRCS})
    list(REMOVE_ITEM HEADERS ${SIG_HEADERS})
endif()

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore)
target_link_libraries(precompiled PUBLIC storage devcore JsonCpp)

if(APPLE)
    target_link_libraries(precompiled PRIVATE Paillier VRF)
else()
    target_link_libraries(precompiled PRIVATE Paillier GroupSig VRF)
endif()

add_subdirectory(extension)
