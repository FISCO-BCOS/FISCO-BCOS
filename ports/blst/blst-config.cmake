# blst-config.cmake - Creates the plain 'blst' imported target
# (compatible with the original ExternalProject target name)

include_guard(GLOBAL)

if(NOT TARGET blst)
    set(BLST_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
    set(BLST_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../lib/${CMAKE_STATIC_LIBRARY_PREFIX}blst${CMAKE_STATIC_LIBRARY_SUFFIX}")

    add_library(blst STATIC IMPORTED)
    set_target_properties(blst PROPERTIES
        IMPORTED_LOCATION "${BLST_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BLST_INCLUDE_DIR}"
    )
endif()
