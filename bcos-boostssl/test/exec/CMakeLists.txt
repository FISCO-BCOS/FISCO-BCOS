set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

# OpenSSL
find_package(TASSL REQUIRED MODULE)

add_executable(http-server-sample http_server_sample.cpp)
target_link_libraries(http-server-sample PUBLIC ${BOOSTSSL_TARGET} bcos-utilities::bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(echo-server-sample echo_server_sample.cpp)
target_link_libraries(echo-server-sample PUBLIC ${BOOSTSSL_TARGET} bcos-utilities::bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(echo-client-sample echo_client_sample.cpp)
target_link_libraries(echo-client-sample PUBLIC ${BOOSTSSL_TARGET} bcos-utilities::bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(msg-codec-perf msg_codec_perf.cpp)
target_link_libraries(msg-codec-perf PUBLIC ${BOOSTSSL_TARGET} bcos-utilities::bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(boostssl-delay-perf boostssl_delay_perf.cpp)
target_link_libraries(boostssl-delay-perf PUBLIC ${BOOSTSSL_TARGET} bcos-utilities::bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(boostssl-throughput-perf boostssl_throughput_perf.cpp)
target_link_libraries(boostssl-throughput-perf PUBLIC ${BOOSTSSL_TARGET} bcos-utilities::bcos-utilities OpenSSL::SSL OpenSSL::Crypto)
