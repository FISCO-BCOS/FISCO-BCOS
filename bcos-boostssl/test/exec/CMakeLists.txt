set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

# OpenSSL
hunter_add_package(OpenSSL)
find_package(OpenSSL REQUIRED)

add_executable(http-server-sample http_server_sample.cpp)
target_link_libraries(http-server-sample PUBLIC jsoncpp_lib_static ${BOOSTSSL_WEBSOCKET_TARGET} ${BOOSTSSL_UTILITIES_TARGET} ${BOOSTSSL_CONTEXT_TARGET} ${BOOSTSSL_HTTPSERVER_TARGET} OpenSSL::SSL OpenSSL::Crypto)

add_executable(echo-server-sample echo_server_sample.cpp)
target_link_libraries(echo-server-sample PUBLIC jsoncpp_lib_static ${BOOSTSSL_WEBSOCKET_TARGET} ${BOOSTSSL_UTILITIES_TARGET} ${BOOSTSSL_CONTEXT_TARGET} ${BOOSTSSL_HTTPSERVER_TARGET} OpenSSL::SSL OpenSSL::Crypto)

add_executable(echo-client-sample echo_client_sample.cpp)
target_link_libraries(echo-client-sample PUBLIC jsoncpp_lib_static ${BOOSTSSL_WEBSOCKET_TARGET} ${BOOSTSSL_UTILITIES_TARGET} ${BOOSTSSL_CONTEXT_TARGET} ${BOOSTSSL_HTTPSERVER_TARGET} OpenSSL::SSL OpenSSL::Crypto)