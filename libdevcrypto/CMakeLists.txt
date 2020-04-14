file(GLOB_RECURSE HEADERS "*.h")

if (BUILD_GM)
    file(GLOB_RECURSE SRC_LIST "gm/*.cpp") 
else ()
    file(GLOB SRC_LIST "./*.cpp")
endif()

set(INSERT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CommonFunc.cpp)
list(APPEND SRC_LIST ${INSERT_SOURCES})

add_library(devcrypto ${SRC_LIST} ${HEADERS})
eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto PRIVATE Secp256k1 Cryptopp)
target_link_libraries(devcrypto PUBLIC devcore)
