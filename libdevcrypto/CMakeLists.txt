file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcrypto ${SRC_LIST} ${HEADERS})
find_package (OpenSSL)

target_include_directories(devcrypto PUBLIC ${BOOST_INCLUDE_DIR})
eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto devcore Secp256k1 Cryptopp dl)
