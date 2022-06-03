include(Version)
project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE SRCS bcos-crypto/*.cpp)

add_definitions(-DSM2_OPTIMIZE)

add_library(bcos-crypto ${SRCS})
target_include_directories(bcos-crypto PUBLIC .)
target_link_libraries(bcos-crypto PUBLIC tassl::ssl tassl::crypto bcos-utilities wedpr::crypto)

if(TESTS)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(demo)
endif()

# include(GNUInstallDirs)
# install(TARGETS bcos-crypto ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# install(DIRECTORY "src" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")