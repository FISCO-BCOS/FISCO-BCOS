include(ExternalProject)
include(GNUInstallDirs)

if("${CMAKE_HOST_SYSTEM_NAME}" MATCHES "Linux")
    set(SDF_LIB_NAME libsdf-crypto_arm.a)
elseif(APPLE)
    message(FATAL "HSM  SDF only support Linux, the ${CMAKE_HOST_SYSTEM_NAME} ${ARCHITECTURE} is not supported.")
else()
    message(FATAL "HSM  SDF only support Linux, the ${CMAKE_HOST_SYSTEM_NAME} ${ARCHITECTURE} is not supported.")
endif()

EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
set(SDF_LIB_NAME "libsdf-crypto_arm.a")
if("${ARCHITECTURE}" MATCHES "x86_64")
    set(SDF_LIB_NAME "libsdf-crypto_x86.a")
endif()

ExternalProject_Add(libsdf
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    GIT_REPOSITORY https://${URL_BASE}/WeBankBlockchain/hsm-crypto.git
    GIT_TAG        9d8e79a88c8ca8365af75a7a0d107e485c0f7e3c
    BUILD_IN_SOURCE true
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND bash -c "/bin/bash ${CMAKE_SOURCE_DIR}/deps/src/libsdf/install.sh"
)

ExternalProject_Get_Property(libsdf SOURCE_DIR)

set(HSM_INCLUDE_DIR ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${HSM_INCLUDE_DIR})  # Must exist.

set(SDF_LIB "${SOURCE_DIR}/lib/${SDF_LIB_NAME}")

add_library(SDF STATIC IMPORTED GLOBAL)
set_property(TARGET SDF PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HSM_INCLUDE_DIR})
set_property(TARGET SDF PROPERTY IMPORTED_LOCATION ${SDF_LIB})
add_dependencies(SDF libsdf)
unset(SOURCE_DIR)