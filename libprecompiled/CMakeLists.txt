
file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

if (NOT CRYPTO_EXTENSION)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/extension/PaillierPrecompiled.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS})
    set(REMOVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extension/PaillierPrecompiled.cpp)
    list(REMOVE_ITEM SRC_LIST ${REMOVE_SOURCES})
endif()

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PRIVATE blockverifier ethcore)
target_link_libraries(precompiled PUBLIC storage devcore JsonCpp)
if (CRYPTO_EXTENSION)
    target_link_libraries(precompiled PUBLIC Paillier)
endif()

add_subdirectory(extension)
