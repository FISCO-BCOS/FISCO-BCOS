include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(evmone
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        DOWNLOAD_NAME evmone-b004a8f2.tar.gz
        URL https://github.com/FISCO-BCOS/evmone/archive/b004a8f2057bb2b91fabaa3b13449c62241bcaa8.tar.gz
            https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/evmone-b004a8f2.tar.gz
        URL_HASH SHA256=17fa57d444420f128af315b88e35b55747bb19b6bc1ef63ea01360d3e381a261
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=off
                   -DEVMC_ROOT=<INSTALL_DIR>
                   -DHUNTER_USE_CACHE_SERVERS=NO
                #    -DHUNTER_ROOT=${CMAKE_SOURCE_DIR}/deps/src/.hunter
                   -DHUNTER_STATUS_DEBUG=ON
        # BUILD_COMMAND cmake --build . -- -j
        BUILD_IN_SOURCE 1
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmone.a
)

ExternalProject_Get_Property(evmone INSTALL_DIR)
set(EVMONE_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${EVMONE_INCLUDE_DIRS})  # Must exist.
set(EVMONE_LIBRARIES ${INSTALL_DIR}/lib/libevmone.a ${INSTALL_DIR}/lib/libkeccak.a)
add_library(EVMONE INTERFACE IMPORTED)
set_property(TARGET EVMONE PROPERTY INTERFACE_LINK_LIBRARIES ${EVMONE_LIBRARIES})
set_property(TARGET EVMONE PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${EVMONE_INCLUDE_DIRS})
add_dependencies(evmone EVMC)
add_dependencies(EVMONE evmone)
