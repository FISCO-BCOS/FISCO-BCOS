set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(network ${SRC_LIST} ${HEADERS})

eth_use(network OPTIONAL OpenSSL)

target_link_libraries(network devcore devcrypto)
target_link_libraries(network ${OPENSSL_SSL_LIBRARIE} ${OPENSSL_CRYPTO_LIBRARIE})

install(TARGETS network RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
