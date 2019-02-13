include(ExternalProject)

ExternalProject_Add(tbb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/01org/tbb/archive/2019_U3.tar.gz
    URL_HASH SHA256=b2244147bc8159cdd8f06a38afeb42f3237d3fc822555499d7ccfbd4b86f8ece
    BUILD_IN_SOURCE 1
    LOG_BUILD 0
    LOG_INSTALL 0
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make
    INSTALL_COMMAND bash -c "cp ./build/*_release/*.so* ${CMAKE_SOURCE_DIR}/deps/lib && export LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/deps/lib:$LD_LIBRARY_PATH"
)

ExternalProject_Get_Property(tbb SOURCE_DIR)
add_library(TBB STATIC IMPORTED)
set(TBB_INCLUDE_DIR ${SOURCE_DIR}/include/)
set(TBB_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/libtbb.so)

set_property(TARGET TBB PROPERTY IMPORTED_LOCATION ${TBB_LIBRARY})
set_property(TARGET TBB PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${TBB_INCLUDE_DIR})
add_dependencies(TBB tbb)
unset(SOURCE_DIR)
