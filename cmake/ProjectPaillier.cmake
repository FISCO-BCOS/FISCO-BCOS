include(ExternalProject)

if (CRYPTO_EXTENSION)
    ExternalProject_Add(paillier
     PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://github.com/FISCO-BCOS/paillier.git
	    GIT_TAG a2a0e11eb9f4c6eb2d5b7bc3de36eeceda44529a
        BUILD_IN_SOURCE 1
        SOURCE_SUBDIR ./paillier_cpp/
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_POSITION_INDEPENDENT_CODE=on
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
    )

    ExternalProject_Get_Property(paillier SOURCE_DIR)
    add_library(Paillier STATIC IMPORTED)
    set(PAILLIER_LIB_SUFFIX a)
    set(PAILLIER_INCLUDE_DIR ${SOURCE_DIR}/paillier_cpp)
    set(PAILLIER_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/libpaillier.${PAILLIER_LIB_SUFFIX})
    file(MAKE_DIRECTORY ${PAILLIER_INCLUDE_DIR})  # Must exist.
    file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/lib/)  # Must exist.

    set_property(TARGET Paillier PROPERTY IMPORTED_LOCATION ${PAILLIER_LIBRARY})
    set_property(TARGET Paillier PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${PAILLIER_INCLUDE_DIR})
    add_dependencies(Paillier paillier)
    unset(SOURCE_DIR)
endif()