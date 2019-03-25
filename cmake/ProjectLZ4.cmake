include(ExternalProject)

set(LZ4_LIB_SUFFIX a)
set(LZ4_INCLUDE_SUFFIX h)

ExternalProject_Add(lz4
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_NAME lz4-1.8.3.tar.gz
    URL https://github.com/lz4/lz4/archive/v1.8.3.tar.gz
    URL_HASH SHA256=33af5936ac06536805f9745e0b6d61da606a1f8b4cc5c04dd3cbaca3b9b4fc43
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make
    INSTALL_COMMAND bash -c "cp ./lib/*.${LZ4_LIB_SUFFIX}* ${CMAKE_SOURCE_DIR}/deps/lib && cp ./lib/*.${LZ4_INCLUDE_SUFFIX} ${CMAKE_SOURCE_DIR}/deps/include"
)

ExternalProject_Get_Property(lz4 SOURCE_DIR)

add_library(LZ4 STATIC IMPORTED GLOBAL)
set(LZ4_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/deps/include)
set(LZ4_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/liblz4.a)

set_property(TARGET LZ4 PROPERTY IMPORTED_LOCATION ${LZ4_LIBRARY})
set_property(TARGET LZ4 PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${LZ4_INCLUDE_DIR})

file(MAKE_DIRECTORY ${LZ4_INCLUDE_DIR})  # Must exist.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/lib/)  # Must exist.

add_dependencies(LZ4 lz4)

unset(SOURCE_DIR)
