set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_executable(deploy_hello deploy_hello.cpp)
if (NOT WIN32)
   target_compile_options(deploy_hello PRIVATE -Wno-error -Wno-unused-variable)
endif()
target_link_libraries(deploy_hello PUBLIC ${BCOS_CPP_SDK_TARGET} bcos-crypto bcos-boostssl bcos-utilities jsoncpp_static OpenSSL::SSL OpenSSL::Crypto)

add_executable(tx_sign_perf tx_sign_perf.cpp)
target_link_libraries(tx_sign_perf PUBLIC ${BCOS_CPP_SDK_TARGET})

add_executable(random_perf random_perf.cpp)
target_link_libraries(random_perf PUBLIC ${BCOS_CPP_SDK_TARGET})