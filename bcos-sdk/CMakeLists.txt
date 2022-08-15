file(GLOB_RECURSE SRC_LIST "bcos-cpp-sdk/*.cpp")

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
target_link_libraries(${BCOS_CPP_SDK_TARGET} PUBLIC
   wedprcrypto::crypto
   ${TARS_PROTOCOL_TARGET}
   bcos-crypto
   bcos-boostssl
   bcos-utilities
   jsoncpp_static
   OpenSSL::SSL
   OpenSSL::Crypto)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(sample)
endif()

include(GNUInstallDirs)
install(TARGETS ${BCOS_CPP_SDK_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-cpp-sdk" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/" FILES_MATCHING PATTERN "*.h")