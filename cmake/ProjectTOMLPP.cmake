include(ExternalProject)

ExternalProject_Add(tomplplusplus_project
    PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    URL https://${URL_BASE}/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH MD5=c1f32ced14311fe949b9ce7cc3f7a867
)

add_library(tomlplusplus INTERFACE IMPORTED GLOBAL)
add_dependencies(tomlplusplus tomlplusplus_project)