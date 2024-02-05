include(ExternalProject)

if (APPLE)
    set(SED_COMMAND sed -i '')
else()
    set(SED_COMMAND sed -i)
endif()

ExternalProject_Add(paillier
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME paillier-8c9336a4.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/FISCO-BCOS/paillier-lib/archive/8c9336a41e324f361bed60f1259e297db06b441a.tar.gz
        # https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/paillier-1daf3b23.tar.gz
    URL_HASH SHA256=6519a7ed8eed01b4258d13aa8ffb1e34890d905e97572f2ea6201555fa95dde3
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
