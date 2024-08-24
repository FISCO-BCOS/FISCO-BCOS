include(ExternalProject)

ExternalProject_Add(tomlplusplus_project
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    URL https://${URL_BASE}/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH MD5=c1f32ced14311fe949b9ce7cc3f7a867
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

ExternalProject_Get_Property(tomlplusplus_project INSTALL_DIR)
add_library(tomlplusplus INTERFACE IMPORTED GLOBAL)
set_property(TARGET tomlplusplus PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
add_dependencies(tomlplusplus tomlplusplus_project)
unset(INSTALL_DIR)