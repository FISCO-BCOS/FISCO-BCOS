include(ExternalProject)

if (CRYPTO_EXTENSION)
    ExternalProject_Add(paillier
     PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://github.com/FISCO-BCOS/paillier.git
	    GIT_TAG 10ebe553d6d238e6b88fbf722509709db792dcb4
        BUILD_IN_SOURCE 1
        # SOURCE_SUBDIR ./paillier_cpp/
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_POSITION_INDEPENDENT_CODE=on
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
    )

    ExternalProject_Get_Property(paillier INSTALL_DIR)
    add_library(Paillier STATIC IMPORTED)
    set(PAILLIER_INCLUDE_DIR ${INSTALL_DIR}/include)
    set(PAILLIER_LIBRARY ${INSTALL_DIR}/lib/libpaillier.a)
    file(MAKE_DIRECTORY ${PAILLIER_INCLUDE_DIR})  # Must exist.
    file(MAKE_DIRECTORY ${INSTALL_DIR}/lib/)  # Must exist.
    set_property(TARGET Paillier PROPERTY IMPORTED_LOCATION ${PAILLIER_LIBRARY})
    set_property(TARGET Paillier PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${PAILLIER_INCLUDE_DIR})
    add_dependencies(Paillier paillier)
    unset(INSTALL_DIR)
endif()
