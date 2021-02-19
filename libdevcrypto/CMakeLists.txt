file(GLOB_RECURSE HEADERS "*.h")
file(GLOB_RECURSE SRC_LIST "*.cpp")

# set(INSERT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CommonFunc.cpp)
list(APPEND SRC_LIST ${INSERT_SOURCES})

add_library(devcrypto ${SRC_LIST} ${HEADERS})

eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto PRIVATE Secp256k1 Cryptopp libff::ff)
target_link_libraries(devcrypto PUBLIC devcore)
if (SDF)
    target_link_libraries(devcrypto PUBLIC "/lib/libswsds.so")
endif()