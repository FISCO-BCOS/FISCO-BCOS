set(ITTAPI_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")

# Replicate the old ExternalProject behavior:
# - WITH_VTUNE_ITT=ON:  STATIC imported library with real ittnotify symbols
# - WITH_VTUNE_ITT=OFF (default): INTERFACE library (no symbols, headers only)
# This avoids duplicate symbol errors with TBB's built-in ITT stubs (itt_notify.cpp.o)
if(WITH_VTUNE_ITT)
    set(ITTAPI_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../lib/${CMAKE_STATIC_LIBRARY_PREFIX}ittnotify${CMAKE_STATIC_LIBRARY_SUFFIX}")
    add_library(ittapi STATIC IMPORTED GLOBAL)
    set_target_properties(ittapi PROPERTIES
        IMPORTED_LOCATION "${ITTAPI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ITTAPI_INCLUDE_DIR}"
    )
else()
    add_library(ittapi INTERFACE IMPORTED GLOBAL)
    set_target_properties(ittapi PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ITTAPI_INCLUDE_DIR}"
    )
endif()
