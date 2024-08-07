file(GLOB_RECURSE SRC_LIST "bcos-cpp-sdk/*.cpp")
list(APPEND LINK_LIB_LIST bcos-crypto bcos-boostssl bcos-utilities ${TARS_PROTOCOL_TARGET} jsoncpp_static OpenSSL::SSL OpenSSL::Crypto)
if (ONLY_CPP_SDK AND (NOT WITH_SWIG_SDK))
    list(REMOVE_ITEM SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/bcos-cpp-sdk/tarsRPC/CoRPCClient.cpp")
    list(REMOVE_ITEM SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/bcos-cpp-sdk/tarsRPC/RPCClient.cpp")
    list(REMOVE_ITEM SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/bcos-cpp-sdk/tarsRPC/detail/Core.cpp")
endif ()

find_package(Boost REQUIRED log serialization)
find_package(wedprcrypto REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Microsoft.GSL REQUIRED)
find_package(range-v3 REQUIRED)

set(BCOS_CPP_SDK_TARGET bcos-cpp-sdk)
add_library(${BCOS_CPP_SDK_TARGET} ${SRC_LIST})
target_include_directories(${BCOS_CPP_SDK_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-cpp-sdk>)
target_link_libraries(${BCOS_CPP_SDK_TARGET} PUBLIC ${LINK_LIB_LIST})
set_target_properties(${BCOS_CPP_SDK_TARGET} PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(sample)
endif()

include(GNUInstallDirs)
install(TARGETS ${BCOS_CPP_SDK_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-cpp-sdk" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/" FILES_MATCHING PATTERN "*.h")

if(WITH_SWIG_SDK)
    add_subdirectory(SWIG)
endif()