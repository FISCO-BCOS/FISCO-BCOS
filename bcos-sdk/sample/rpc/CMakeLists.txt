set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_executable(blocknotifier blocknotifier.cpp)
if (NOT WIN32)
   target_compile_options(blocknotifier PRIVATE -Wno-error -Wno-unused-variable)
endif()
target_link_libraries(blocknotifier PUBLIC ${BCOS_CPP_SDK_TARGET} bcos-boostssl bcos-utilities jsoncpp_static OpenSSL::SSL OpenSSL::Crypto)