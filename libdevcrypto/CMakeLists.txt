file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcrypto ${SRC_LIST} ${HEADERS})

target_include_directories(devcrypto PUBLIC ${BOOST_INCLUDE_DIR})
target_link_libraries(devcrypto devcore Secp256k1 Cryptopp)
find_package(OpenSSL)
target_link_libraries(devcrypto ${OPENSSL_LIBRARIES})
