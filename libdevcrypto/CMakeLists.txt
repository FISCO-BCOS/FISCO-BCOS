
if (BUILD_GM)
    file(GLOB_RECURSE SRC_LIST "gm/*.cpp") 
    file(GLOB_RECURSE HEADERS "*.h")  
else()
    file(GLOB SRC_LIST "./*.cpp")
    file(GLOB HEADERS "*.h")
endif()

add_library(devcrypto ${SRC_LIST} ${HEADERS})
eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto devcore Secp256k1 Cryptopp dl)
target_include_directories(devcrypto PUBLIC ${BOOST_INCLUDE_DIR})
