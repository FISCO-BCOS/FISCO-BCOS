file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

aux_source_directory(. SRC_LIST)
aux_source_directory(./libnetwork SRC_LIST)
aux_source_directory(./libp2p SRC_LIST)
aux_source_directory(./libamop SRC_LIST)

add_library(${BCOS_GATEWAY_TARGET} ${SRC_LIST} ${HEADERS})
target_link_libraries(${BCOS_GATEWAY_TARGET} PUBLIC bcos-framework::utilities jsoncpp_lib_static OpenSSL::SSL OpenSSL::Crypto bcos-tars-protocol::protocol-tars tarscpp::tarsservant)

target_compile_options(${BCOS_GATEWAY_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
if (APPLE)
target_compile_options(${BCOS_GATEWAY_TARGET} PRIVATE -faligned-allocation)
endif()
