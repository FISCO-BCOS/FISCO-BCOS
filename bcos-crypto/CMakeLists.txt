
project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE ALL_SRCS bcos-crypto/*.cpp)
set(LIB_LIST OpenSSL::SSL OpenSSL::Crypto wedprcrypto::crypto wedprcrypto::zkp bcos-utilities bcos-concepts TBB::tbb SDF)

find_package(OpenSSL REQUIRED)
find_package(TBB REQUIRED)
find_package(wedprcrypto REQUIRED)

add_library(bcos-crypto STATIC ${ALL_SRCS})
target_link_libraries(bcos-crypto PUBLIC ${LIB_LIST})

if(TESTS)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(demo)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-crypto EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-crypto" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")