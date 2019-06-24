include(ExternalProject)

set(TCMALLOC_CONFIG ./autogen.sh COMMAND ./configure  --disable-shared --disable-tests CXXFLAGS=-DHAVE_POSIX_MEMALIGN_SYMBOL=1 --enable-frame-pointers --disable-cpu-profiler --disable-heap-profiler --disable-heap-checker --disable-debugalloc --enable-minimal)

if (APPLE)
    set(TCMALLOC_MAKE make)
else()
    set(TCMALLOC_MAKE make)
endif()

ExternalProject_Add(gptcmalloc
    PREFIX ${CMAKE_SOURCE_DIR}/deps/
    DOWNLOAD_NAME master.zip
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/gperftools/gperftools/archive/master.zip
    URL_HASH SHA256=2ad5c0685585e1ea818c4a254d97efe6d63644ec32376035d1702e5123c7c94b
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ${TCMALLOC_CONFIG} 
    BUILD_COMMAND ${TCMALLOC_MAKE}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/.libs/libtcmalloc_minimal.a
)

ExternalProject_Get_Property(gptcmalloc SOURCE_DIR)
add_library(tcmalloc STATIC IMPORTED GLOBAL)

set(TCMALLOC_INCLUDE_DIR ${SOURCE_DIR}/include/)
set(TCMALLOC_LIBRARY ${SOURCE_DIR}/.libs/libtcmalloc_minimal.a)
file(MAKE_DIRECTORY ${TCMALLOC_INCLUDE_DIR})  # Must exist.

set_property(TARGET tcmalloc PROPERTY IMPORTED_LOCATION ${TCMALLOC_LIBRARY})
set_property(TARGET tcmalloc PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${TCMALLOC_INCLUDE_DIR})

unset(SOURCE_DIR)
