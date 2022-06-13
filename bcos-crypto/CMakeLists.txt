include(Version)
project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE SRCS bcos-crypto/*.cpp)

find_package(OpenSSL REQUIRED)

add_library(bcos-crypto ${SRCS})
target_include_directories(bcos-crypto PUBLIC .)
target_link_libraries(bcos-crypto PUBLIC OpenSSL::SSL OpenSSL::Crypto bcos-utilities wedpr::crypto)

if(TESTS)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(demo)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-crypto ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-crypto" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bcos-crypto" FILES_MATCHING PATTERN "*.h")