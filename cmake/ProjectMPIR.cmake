include(ExternalProject)

set(prefix "${CMAKE_BINARY_DIR}/deps")
set(MPIR_LIBRARY "${prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}mpir${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(MPIR_INCLUDE_DIR "${prefix}/include")

ExternalProject_Add(mpir
    PREFIX "${prefix}"
    DOWNLOAD_NAME mpir-cmake-47910ac6.tar.gz
    DOWNLOAD_NO_PROGRESS TRUE
    URL https://github.com/FISCO-BCOS/mpir/archive/47910ac631c599774577b155a33fa3a372155c1d.tar.gz
        https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/mpir-cmake-47910ac6.tar.gz
    URL_HASH SHA256=f09fab14ce96624ddfb2803f515326b9fbb503010258fd8b74d290ea205b463c
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DMPIR_GMP=On
    LOG_CONFIGURE 1
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_BYPRODUCTS "${MPIR_LIBRARY}"
)

add_library(MPIR::mpir STATIC IMPORTED)
set_property(TARGET MPIR::mpir PROPERTY IMPORTED_CONFIGURATIONS Release)
set_property(TARGET MPIR::mpir PROPERTY IMPORTED_LOCATION_RELEASE ${MPIR_LIBRARY})
set_property(TARGET MPIR::mpir PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MPIR_INCLUDE_DIR})
add_dependencies(MPIR::mpir mpir)
