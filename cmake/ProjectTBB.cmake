include(ExternalProject)

set(TBB_LIB_SUFFIX a)
if (APPLE)
    set(ENABLE_STD_LIB stdlib=libc++)
endif()

ExternalProject_Add(tbb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_NAME v2021.6.0-rc1
    # TODO: add wb cdn link
    URL https://codeload.github.com/oneapi-src/oneTBB/tar.gz/refs/tags/v2021.6.0-rc1
    URL_HASH SHA1=7653c3db34db9e132286e69f1f69da1a6b39df9f
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CMAKE_COMMAND ${CMAKE_COMMAND}
    CMAKE_ARGS 
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> 
        -DBUILD_SHARED_LIBS=OFF
        -DTBB_ENABLE_IPO=OFF
        -DTBB_TEST=OFF 
        -DTBB_EXAMPLES=OFF 
        -DTBB_BENCH=OFF
    #CONFIGURE_COMMAND ""
    BUILD_COMMAND make extra_inc=big_iron.inc ${ENABLE_STD_LIB}
    INSTALL_COMMAND bash -c "/bin/cp -f */libtbb.${TBB_LIB_SUFFIX}* ${CMAKE_SOURCE_DIR}/deps/lib/"
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libtbb.${TBB_LIB_SUFFIX}
)

ExternalProject_Get_Property(tbb SOURCE_DIR)
add_library(TBB STATIC IMPORTED)
set(TBB_INCLUDE_DIR ${SOURCE_DIR}/include)
set(TBB_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/libtbb.${TBB_LIB_SUFFIX})
file(MAKE_DIRECTORY ${TBB_INCLUDE_DIR})  # Must exist.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/lib/)  # Must exist.

set_property(TARGET TBB PROPERTY IMPORTED_LOCATION ${TBB_LIBRARY})
set_property(TARGET TBB PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${TBB_INCLUDE_DIR})
add_dependencies(TBB tbb)
unset(SOURCE_DIR)
