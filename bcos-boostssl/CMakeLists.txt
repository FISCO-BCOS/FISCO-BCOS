include(Version)
project(bcos-boostssl VERSION ${VERSION})

file(GLOB_RECURSE SRCS bcos-boostssl/*.cpp)

# To be change to tassl
find_package(TASSL REQUIRED)

add_library(bcos-boostssl ${SRCS})
target_link_libraries(bcos-boostssl PUBLIC bcos-framework bcos-utilities TASSL::SSL)

if(TESTS)
    # enable_testing()
    # add_subdirectory(test)
endif()

# include(GNUInstallDirs)
# install(TARGETS bcos-utilities ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# install(DIRECTORY "src" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")