
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
endif ()

# aux_source_directory(bcos-utilities SRCS)
file(GLOB_RECURSE SRCS "bcos-utilities/*.cpp")
if (ONLY_CPP_SDK)
    list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/ZstdCompress.cpp")
    list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bcos-utilities/ITTAPI.h")
endif ()

add_library(bcos-utilities ${SRCS})
if (WITH_CXX_MODULES)
    # Named-module build: the module interface unit (module bcos.utilities)
    # is compiled once into this library; consumers get the BMI on their
    # compile line by linking bcos-utilities (the FISCO_WITH_CXX_MODULES
    # define that switches their guarded `import bcos.utilities;` on is set
    # globally by CompilerSettings.cmake).
    target_sources(bcos-utilities PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES bcos-utilities/Utilities.cppm)
endif ()
target_include_directories(bcos-utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-utilities>)
if (ONLY_CPP_SDK)
    target_link_libraries(bcos-utilities PUBLIC Boost::iostreams Boost::log_setup Boost::log Boost::filesystem Boost::chrono Boost::thread Boost::serialization Boost::${BOOST_STACKTRACE_COMPONENT} ZLIB::ZLIB)
else ()
    target_link_libraries(bcos-utilities PUBLIC bcos-task Boost::iostreams Boost::log_setup Boost::log Boost::filesystem Boost::chrono Boost::thread Boost::serialization Boost::${BOOST_STACKTRACE_COMPONENT} ZLIB::ZLIB zstd::libzstd_static ittapi)
endif ()
set_target_properties(bcos-utilities PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
if (WITH_CXX_MODULES)
    install(TARGETS bcos-utilities EXPORT fiscobcosTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
else ()
    install(TARGETS bcos-utilities EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif ()
install(DIRECTORY "bcos-utilities" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/" FILES_MATCHING PATTERN "*.h")