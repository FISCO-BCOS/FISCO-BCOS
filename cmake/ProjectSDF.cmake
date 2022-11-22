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
    GIT_TAG        e270f9cc577f3826432595375e480f59ba617592
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
message(status "##### HSM_INCLUDE_DIR: ${HSM_INCLUDE_DIR}")
message("#### SDF_LIB: ${SDF_LIB}")
find_library(GMT0018 gmt0018)
if(NOT GMT0018)
    message(FATAL " Can not find library libgmt0018.so under default library path, please make sure you have a crypto PCI card on your machine, as well as the the driver and libraries are installed.")
endif()

add_library(SDF STATIC IMPORTED GLOBAL)
set_property(TARGET SDF PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HSM_INCLUDE_DIR})
set_property(TARGET SDF PROPERTY IMPORTED_LOCATION ${SDF_LIB})
add_dependencies(SDF libsdf)
unset(SOURCE_DIR)