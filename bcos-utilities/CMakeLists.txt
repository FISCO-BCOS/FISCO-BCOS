include(Version)
project(bcos-utilities VERSION ${VERSION})

aux_source_directory(src SRCS)

add_library(bcos-utilities ${SRCS})
target_link_libraries(bcos-utilities PUBLIC Boost::log Boost::chrono Boost::thread)

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-utilities ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "src" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")