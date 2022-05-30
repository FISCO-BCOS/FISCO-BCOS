include(Version)
project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE SRCS bcos-crypto/*.cpp)

find_package(OpenSSL REQUIRED Crypto SSL)

add_library(bcos-crypto ${SRCS})
target_include_directories(bcos-crypto PUBLIC .)
target_link_libraries(bcos-crypto PUBLIC bcos-utilities WeDPRCrypto OpenSSL::Crypto)

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# include(GNUInstallDirs)
# install(TARGETS bcos-crypto ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# install(DIRECTORY "src" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")