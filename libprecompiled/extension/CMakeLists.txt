
file(GLOB HEADERS "*.h")
set(USER_PRECOMPILED_HEADER "")

if (NOT CRYPTO_EXTENSION)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/PaillierPrecompiled.h)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/GroupSigPrecompiled.h)
    set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/RingSigPrecompiled.h)
    list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS})
endif()

foreach(header ${HEADERS})
    string (REPLACE "${CMAKE_SOURCE_DIR}/" "" header "${header}")
    list(APPEND USER_PRECOMPILED_HEADER "#include <${header}>")
endforeach()
string (REPLACE ";" "\n" USER_PRECOMPILED_HEADER "${USER_PRECOMPILED_HEADER}")
configure_file(${FISCO_BCOS_CMAKE_DIR}/templates/UserPrecompiled.h.in ${CMAKE_BINARY_DIR}/include/UserPrecompiled.h @ONLY)
