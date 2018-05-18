#------------------------------------------------------------------------------
# CMake helper for libdevcore, libdevcrypto and libp2p modules.
#
# This module defines
#     Dev_XXX_LIBRARIES, the libraries needed to use ethereum.
#     Dev_INCLUDE_DIRS
#
# The documentation for cpp-ethereum is hosted at http://cpp-ethereum.org
#
# ------------------------------------------------------------------------------
include(ExternalProject)
include(GNUInstallDirs)


ExternalProject_Add(group_sig_lib
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME group_sig_lib.tgz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/xxx/group_sig_lib.tgz
    URL_HASH SHA256=356ed22038c84d92d06fff0753867b6df8935956cc9382f99f4ca28b1fc1cb55
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(group_sig_lib SOURCE_DIR)
set(GROUP_SIG_INCLUDE_DIRS ${SOURCE_DIR})
set(GROUP_SIG_LIB_DIRS ${SOURCE_DIR})
unset(BUILD_DIR)
set(GROUP_SIG_LIB_SUFFIX .a)

find_package(PBC REQUIRED)

add_library(group_sig_devcore STATIC IMPORTED)
set_property(TARGET group_sig_devcore PROPERTY IMPORTED_LOCATION ${GROUP_SIG_LIB_DIRS}/devcore/libgroup_sig_devcore${GROUP_SIG_LIB_SUFFIX})
add_dependencies(group_sig_devcore group_sig_lib)

add_library(group_sig STATIC IMPORTED)
set_property(TARGET group_sig PROPERTY IMPORTED_LOCATION ${GROUP_SIG_LIB_DIRS}/algorithm/libgroup_sig${GROUP_SIG_LIB_SUFFIX})
add_dependencies(group_sig group_sig_lib)
