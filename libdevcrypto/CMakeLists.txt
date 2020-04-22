file(GLOB_RECURSE HEADERS "*.h")
file(GLOB_RECURSE SRC_LIST "*.cpp")

if (BUILD_GM)
    # file(GLOB_RECURSE SRC_LIST "gm/*.cpp") 
    list(REMOVE_ITEM SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/Common.cpp)
    list(REMOVE_ITEM SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/Hash.cpp)
else()
    list(REMOVE_ITEM SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/gm/GmCommon.cpp)
    list(REMOVE_ITEM SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/gm/GmHash.cpp)
endif()

set(INSERT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CommonFunc.cpp ${CMAKE_CURRENT_SOURCE_DIR}/HashCommonFunc.cpp)
list(APPEND SRC_LIST ${INSERT_SOURCES})

add_library(devcrypto ${SRC_LIST} ${HEADERS})
eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto PRIVATE Secp256k1 Cryptopp)
target_link_libraries(devcrypto PUBLIC devcore)
