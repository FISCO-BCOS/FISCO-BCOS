project(bcos-devp2p VERSION ${VERSION})

file(GLOB_RECURSE ALL_SRCS bcos-devp2p/*.cpp)

find_package(Snappy REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

add_library(bcos-devp2p STATIC ${ALL_SRCS})
target_include_directories(bcos-devp2p PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-devp2p>)
target_link_libraries(bcos-devp2p PUBLIC
    bcos-crypto
    codec
    bcos-utilities
    bcos-protocol
    rlp-protocol
    Snappy::snappy
    OpenSSL::Crypto
    jsoncpp_static)
set_target_properties(bcos-devp2p PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-devp2p EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-devp2p" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
