include(ExternalProject)

ExternalProject_Add(tomlplusplus_project
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

ExternalProject_Get_Property(tomlplusplus_project INSTALL_DIR)
add_library(tomlplusplus INTERFACE IMPORTED GLOBAL)
set_property(TARGET tomlplusplus PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
add_dependencies(tomlplusplus tomlplusplus_project)
unset(INSTALL_DIR)