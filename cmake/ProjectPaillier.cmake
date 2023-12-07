include(ExternalProject)

if (APPLE)
    set(SED_COMMAND sed -i '')
else()
    set(SED_COMMAND sed -i)
endif()

ExternalProject_Add(paillier
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME paillier-e1944826.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    # URL https://github.com/FISCO-BCOS/paillier-lib/archive/1daf3b23b01121e8522a8b264be933f6d236fdb8.tar.gz
        # https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/paillier-1daf3b23.tar.gz
    # URL_HASH SHA256=574c8315961ea2ba9534a739675172a0e580ca140c9b2a6fb1008aaf608ae1c9
    URL https://github.com/Shareong/paillier-lib/archive/e1944826ba291a603ff705b3ff8c0cbafa55dcd5.tar.gz
    URL_HASH SHA256=bc0cd2d5391961c466651558136432134dd4012e4fdfd4df9d2bcda5fb8404dd

    BUILD_IN_SOURCE 1
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release
    PATCH_COMMAND ${SED_COMMAND} "s/if ((pkLen != 512 && pkLen != 1024) || cipher1.length() != cipherLen)/if(cipher1.length() != cipherLen)/g" paillierCpp/callpaillier.cpp
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
)

ExternalProject_Get_Property(paillier INSTALL_DIR)
add_library(Paillier STATIC IMPORTED)
set(PAILLIER_LIBRARY ${INSTALL_DIR}/lib/libpaillier.a)
set(PAILLIER_INCLUDE_DIR ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${INSTALL_DIR}/lib)  # Must exist.
file(MAKE_DIRECTORY ${PAILLIER_INCLUDE_DIR})  # Must exist.
set_property(TARGET Paillier PROPERTY IMPORTED_LOCATION ${PAILLIER_LIBRARY})
set_property(TARGET Paillier PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${PAILLIER_INCLUDE_DIR})
add_dependencies(Paillier paillier)
unset(INSTALL_DIR)
