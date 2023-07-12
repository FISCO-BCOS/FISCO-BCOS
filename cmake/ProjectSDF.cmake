include(ExternalProject)
include(GNUInstallDirs)

EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
set(SDF_LIB_NAME "libsdf-crypto.a")

ExternalProject_Add(libsdf
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    GIT_REPOSITORY https://${URL_BASE}/WeBankBlockchain/hsm-crypto.git
    GIT_TAG        de061fc70adac68e0a490905d26ed01e0cbbf5e8
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    BUILD_IN_SOURCE true
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
)

ExternalProject_Get_Property(libsdf INSTALL_DIR)

set(HSM_INCLUDE_DIR ${INSTALL_DIR}/include/)
file(MAKE_DIRECTORY ${HSM_INCLUDE_DIR})  # Must exist.

set(SDF_LIB "${INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/${SDF_LIB_NAME}")

add_library(SDF STATIC IMPORTED GLOBAL)
set_property(TARGET SDF PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HSM_INCLUDE_DIR})
set_property(TARGET SDF PROPERTY IMPORTED_LOCATION ${SDF_LIB})
add_dependencies(SDF libsdf)
unset(INSTALL_DIR)