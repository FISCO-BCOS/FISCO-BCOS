set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_executable(publish publish.cpp)
if (NOT WIN32)
   target_compile_options(publish PRIVATE -Wno-error -Wno-unused-variable)
endif()
target_link_libraries(publish PUBLIC ${BCOS_CPP_SDK_TARGET} bcos-boostssl bcos-utilities jsoncpp_static OpenSSL::SSL OpenSSL::Crypto)

add_executable(broadcast broadcast.cpp)
if (NOT WIN32)
   target_compile_options(broadcast PRIVATE -Wno-error -Wno-unused-variable)
endif()
target_link_libraries(broadcast PUBLIC ${BCOS_CPP_SDK_TARGET} bcos-boostssl bcos-utilities jsoncpp_static OpenSSL::SSL OpenSSL::Crypto)

add_executable(subscribe subscribe.cpp)
if (NOT WIN32)
   target_compile_options(subscribe PRIVATE -Wno-error -Wno-unused-variable)
endif()
target_link_libraries(subscribe PUBLIC ${BCOS_CPP_SDK_TARGET} bcos-boostssl bcos-utilities jsoncpp_static OpenSSL::SSL OpenSSL::Crypto)