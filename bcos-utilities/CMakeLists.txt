
project(bcos-utilities VERSION ${VERSION})

find_package(Boost REQUIRED COMPONENTS log filesystem chrono thread serialization iostreams)
find_package(ZLIB REQUIRED)
if(NOT ONLY_CPP_SDK)
    find_package(zstd CONFIG REQUIRED)
    find_package(redis++ CONFIG REQUIRED)
endif ()

# aux_source_directory(bcos-utilities SRCS)
file(GLOB_RECURSE SRCS "bcos-utilities/*.cpp")
if (ONLY_CPP_SDK)
    list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/ratelimiter/DistributedRateLimiter.cpp")
    list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/ZstdCompress.cpp")
    list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/ITTAPI.h")
endif ()

add_library(bcos-utilities ${SRCS})
target_include_directories(bcos-utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-utilities>)
if (ONLY_CPP_SDK)
    target_link_libraries(bcos-utilities PUBLIC Boost::iostreams Boost::log Boost::filesystem Boost::chrono Boost::thread Boost::serialization ZLIB::ZLIB)
else ()
    target_link_libraries(bcos-utilities PUBLIC Boost::iostreams Boost::log Boost::filesystem Boost::chrono Boost::thread Boost::serialization redis++::redis++_static ZLIB::ZLIB zstd::libzstd_static ittapi)
endif ()
set_target_properties(bcos-utilities PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-utilities EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-utilities" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/" FILES_MATCHING PATTERN "*.h")