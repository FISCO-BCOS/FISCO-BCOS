# paillier-config.cmake - Creates the plain 'Paillier' imported target

set(PAILLIER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
set(PAILLIER_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../lib/${CMAKE_STATIC_LIBRARY_PREFIX}paillier${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(NOT TARGET Paillier)
    add_library(Paillier STATIC IMPORTED GLOBAL)
    set_target_properties(Paillier PROPERTIES
        IMPORTED_LOCATION "${PAILLIER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PAILLIER_INCLUDE_DIR}"
    )
endif()
