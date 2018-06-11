aux_source_directory(. SRC_LIST)

if(ENCRYPTTYPE)
	aux_source_directory(./sm2 SRC_LIST)
	aux_source_directory(./sm3 SRC_LIST)
	aux_source_directory(./sm4 SRC_LIST)
	include_directories(./sm2)
	include_directories(./sm3)
	include_directories(./sm4)
endif()

file(GLOB HEADERS "*.h")

add_library(devcrypto ${SRC_LIST} ${HEADERS})
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)

find_package(Dev)
find_package(Utils)

target_include_directories(devcrypto PRIVATE ..)
target_include_directories(devcrypto PRIVATE ../utils)

target_link_libraries(devcrypto Secp256k1 Cryptopp ${OPENSSL_CRYPTO_LIBRARIE} ${Utils_SCRYPT_LIBRARIES} devcore dl)