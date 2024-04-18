include(ExternalProject)
include(GNUInstallDirs)


ExternalProject_Add(blst
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        GIT_REPOSITORY https://${URL_BASE}/supranational/blst.git
        GIT_TAG 3dd0f804b1819e5d03fb22ca2e6fac105932043a

        BUILD_IN_SOURCE true

        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        LOG_BUILD true
        LOG_INSTALL true
        LOG_CONFIGURE true
        )

ExternalProject_Get_Property(blst SOURCE_DIR)



