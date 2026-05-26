# fbwasm-config.cmake - Creates the plain 'fbwasm' imported target
# (compatible with the original ExternalProject target name)

set(FBWASM_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
set(FBWASM_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../lib/${CMAKE_STATIC_LIBRARY_PREFIX}bcos_wasm${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(NOT TARGET fbwasm)
    add_library(fbwasm STATIC IMPORTED GLOBAL)
    set_target_properties(fbwasm PROPERTIES
        IMPORTED_LOCATION "${FBWASM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FBWASM_INCLUDE_DIR}"
    )
endif()
