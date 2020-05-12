include(ExternalProject)

set(MHD_CONFIG ./configure --enable-spdy=no --disable-curl --disable-messages --disable-postprocessor --enable-https=no)
set(MHD_BUILD make)

ExternalProject_Add(mhd
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_NAME libmicrohttpd-0.9.63.tar.gz
    URL https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-0.9.63.tar.gz
        https://mirrors.ustc.edu.cn/gnu/libmicrohttpd/libmicrohttpd-0.9.63.tar.gz 
        https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/libmicrohttpd-0.9.63.tar.gz
    URL_HASH SHA256=37c36f1be177f0e37ef181a645cd3baac1000bd322a01c2eff70f3cc8c91749c
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ${MHD_CONFIG}
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/src/microhttpd/.libs/libmicrohttpd.a
)

ExternalProject_Get_Property(mhd SOURCE_DIR)
add_library(MHD STATIC IMPORTED)
set(MHD_INCLUDE_DIR ${SOURCE_DIR}/src/include/)
set(MHD_LIBRARY ${SOURCE_DIR}/src/microhttpd/.libs/libmicrohttpd.a)
file(MAKE_DIRECTORY ${MHD_INCLUDE_DIR})  # Must exist.

set_property(TARGET MHD PROPERTY IMPORTED_LOCATION ${MHD_LIBRARY})
set_property(TARGET MHD PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MHD_INCLUDE_DIR})
add_dependencies(MHD mhd)
unset(SOURCE_DIR)
