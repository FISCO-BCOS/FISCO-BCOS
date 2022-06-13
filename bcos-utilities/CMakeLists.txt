include(Version)
project(bcos-utilities VERSION ${VERSION})

aux_source_directory(bcos-utilities SRCS)

find_package(Boost REQUIRED COMPONENTS log filesystem chrono thread)

add_library(bcos-utilities ${SRCS})
target_include_directories(bcos-utilities PUBLIC .)
target_link_libraries(bcos-utilities PUBLIC Boost::log Boost::filesystem Boost::chrono Boost::thread)

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-utilities ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-utilities" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bcos-utilities" FILES_MATCHING PATTERN "*.h")