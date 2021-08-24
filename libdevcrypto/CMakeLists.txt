aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")

aux_source_directory(./sm2 SRC_LIST)
include_directories(./sm2)

aux_source_directory(./sm3 SRC_LIST)
include_directories(./sm3)

aux_source_directory(./sm4 SRC_LIST)
include_directories(./sm4)

if (USE_HSM_SDF)
aux_source_directory(./hsm SRC_LIST)
include_directories(./hsm)
endif()

add_library(devcrypto ${SRC_LIST} ${HEADERS})
eth_use(devcrypto OPTIONAL OpenSSL)
target_link_libraries(devcrypto PRIVATE Secp256k1 Cryptopp libff::ff)
target_link_libraries(devcrypto PUBLIC devcore)
if (USE_HSM_SDF)
    target_link_libraries(devcrypto PUBLIC SDF)
    target_link_libraries(devcrypto PUBLIC "/usr/lib/libswsds.so")
endif()