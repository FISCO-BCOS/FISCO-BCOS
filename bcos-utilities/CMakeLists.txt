
project(bcos-utilities VERSION ${VERSION})

# Select proper Boost.Stacktrace backend per platform
set(BOOST_STACKTRACE_COMPONENT stacktrace_backtrace)
if (WIN32)
    # On Windows, use windbg backend; prefer cached with MSVC
    if (MSVC)
        set(BOOST_STACKTRACE_COMPONENT stacktrace_windbg_cached)
    else()
        set(BOOST_STACKTRACE_COMPONENT stacktrace_windbg)
    endif()
endif()

find_package(Boost REQUIRED COMPONENTS log log_setup filesystem chrono thread serialization iostreams ${BOOST_STACKTRACE_COMPONENT})
find_package(ZLIB REQUIRED)
if(NOT ONLY_CPP_SDK)
    find_package(zstd CONFIG REQUIRED)
    find_package(redis++ CONFIG REQUIRED)
endif ()

# Collect all .cpp sources from subdirectories (except config/)
file(GLOB_RECURSE UTILITY_SRCS
    "bcos-utilities/core/*.cpp"
    "bcos-utilities/exceptions/*.cpp"
    "bcos-utilities/logging/*.cpp"
    "bcos-utilities/data/*.cpp"
    "bcos-utilities/compress/*.cpp"
    "bcos-utilities/concurrency/*.cpp"
    "bcos-utilities/timer/*.cpp"
    "bcos-utilities/ratelimiter/*.cpp"
    "bcos-utilities/rate/*.cpp"
    "bcos-utilities/filesystem/*.cpp"
    "bcos-utilities/topology/*.cpp"
)
if (ONLY_CPP_SDK)
    list(REMOVE_ITEM UTILITY_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/ratelimiter/DistributedRateLimiter.cpp")
    list(REMOVE_ITEM UTILITY_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/compress/ZstdCompress.cpp")
endif ()

# === bcos-utilities library (no bcos-framework dependency) ===
add_library(bcos-utilities ${UTILITY_SRCS})
target_include_directories(bcos-utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-utilities>)
if (ONLY_CPP_SDK)
    target_link_libraries(bcos-utilities PUBLIC Boost::iostreams Boost::log_setup Boost::log Boost::filesystem Boost::chrono Boost::thread Boost::serialization Boost::${BOOST_STACKTRACE_COMPONENT} ZLIB::ZLIB)
else ()
    target_link_libraries(bcos-utilities PUBLIC bcos-task Boost::iostreams Boost::log_setup Boost::log Boost::filesystem Boost::chrono Boost::thread Boost::serialization Boost::${BOOST_STACKTRACE_COMPONENT} redis++::redis++_static ZLIB::ZLIB zstd::libzstd_static ittapi)
endif ()
# Disable unity build for bcos-utilities to avoid range-v3 / C++20 std::ranges conflict on GCC 14+
# (vcpkg's range-v3 is incompatible with GCC 14's C++20 concepts/ranges)
set_target_properties(bcos-utilities PROPERTIES UNITY_BUILD "OFF")

# === bcos-config sub-library (depends on bcos-framework/bcos-protocol) ===
# Contains NodeConfig and BfsFileFactory originally from bcos-tool
find_package(jsoncpp CONFIG REQUIRED)
find_package(tarscpp REQUIRED)
find_package(TBB REQUIRED)

file(GLOB CONFIG_SRCS "bcos-utilities/config/*.cpp")
add_library(bcos-config ${CONFIG_SRCS})
target_include_directories(bcos-config PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/bcos-protocol>)
target_link_libraries(bcos-config PUBLIC
    table
    bcos-utilities
    bcos-framework
    bcos-protocol
    jsoncpp_static
    tarscpp::tarsservant
    tarscpp::tarsutil
    TBB::tbb)
set_target_properties(bcos-config PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-utilities bcos-config EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-utilities" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/" FILES_MATCHING PATTERN "*.h")