# c-kzg-4844-config.cmake - Creates the plain 'ckzg' imported target
# (mirrors the blst overlay port style)

include_guard(GLOBAL)

include(CMakeFindDependencyMacro)
find_dependency(blst CONFIG)

if(NOT TARGET ckzg)
    set(CKZG_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
    set(CKZG_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../lib/${CMAKE_STATIC_LIBRARY_PREFIX}ckzg${CMAKE_STATIC_LIBRARY_SUFFIX}")

    add_library(ckzg STATIC IMPORTED)
    set_target_properties(ckzg PROPERTIES
        IMPORTED_LOCATION "${CKZG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CKZG_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES blst
    )
endif()
