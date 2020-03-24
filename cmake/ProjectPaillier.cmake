include(ExternalProject)

if (CRYPTO_EXTENSION)
    ExternalProject_Add(paillier
     PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://github.com/FISCO-BCOS/paillier-lib.git
	    GIT_TAG 1daf3b23b01121e8522a8b264be933f6d236fdb8
        BUILD_IN_SOURCE 1
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_POSITION_INDEPENDENT_CODE=on
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
endif()
