include(Version)
project(bcos-boostssl VERSION ${VERSION})

file(GLOB_RECURSE SRCS bcos-boostssl/*.cpp)

find_package(OpenSSL REQUIRED)

add_library(bcos-boostssl ${SRCS})
target_include_directories(bcos-boostssl PUBLIC .)
target_link_libraries(bcos-boostssl PUBLIC bcos-framework bcos-utilities OpenSSL::SSL)

if(TESTS)
    # enable_testing()
    # add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-boostssl ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-boostssl" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")