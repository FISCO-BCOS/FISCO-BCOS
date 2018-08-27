file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcrypto ${SRC_LIST} ${HEADERS})

find_package(Utils)

target_include_directories(devcrypto PRIVATE ..)
target_include_directories(devcrypto PRIVATE ../utils)
target_include_directories(devcrypto PUBLIC ${BOOST_INCLUDE_DIR})
target_link_libraries(devcrypto devcore Secp256k1 Cryptopp ${Utils_SCRYPT_LIBRARIES})
