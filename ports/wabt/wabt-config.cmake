# wabt-config.cmake - Creates the plain 'wabt' imported target
# (compatible with the original ExternalProject target name)

set(WABT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
set(WABT_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../lib/${CMAKE_STATIC_LIBRARY_PREFIX}wabt${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(NOT TARGET wabt)
    add_library(wabt STATIC IMPORTED GLOBAL)
    set_target_properties(wabt PROPERTIES
        IMPORTED_LOCATION "${WABT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WABT_INCLUDE_DIR}"
    )
endif()
