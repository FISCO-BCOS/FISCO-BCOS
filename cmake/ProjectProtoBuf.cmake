include(ExternalProject)

set(PROTOBUF_CONFIG ./configure)
set(PROTOBUF_MAKE make)

ExternalProject_Add(protobuf
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME protobuf-cpp-3.9.1.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/protocolbuffers/protobuf/releases/download/v3.9.1/protobuf-cpp-3.9.1.tar.gz
    URL_HASH SHA256=29a1db3b9bebcf054c540f13400563120ff29fbdd849b2c7a097ffe9d3d508eb
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ${PROTOBUF_CONFIG}
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_COMMAND ${PROTOBUF_MAKE}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/src/.libs/libprotobuf.a
)

ExternalProject_Get_Property(protobuf SOURCE_DIR)
add_library(ProtoBuf STATIC IMPORTED GLOBAL)

#set(PROTOBUF_INCLUDE_DIR ${SOURCE_DIR}/src/google/protobuf/)
set(PROTOBUF_INCLUDE_DIR ${SOURCE_DIR}/src/)
set(PROTOBUF_LIBRARY ${SOURCE_DIR}/src/.libs/libprotobuf.a)
file(MAKE_DIRECTORY ${PROTOBUF_INCLUDE_DIR})  # Must exist.

set_property(TARGET ProtoBuf PROPERTY IMPORTED_LOCATION ${PROTOBUF_LIBRARY})
set_property(TARGET ProtoBuf PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${PROTOBUF_INCLUDE_DIR})

add_dependencies(ProtoBuf protobuf)
unset(SOURCE_DIR)