
file(GLOB_RECURSE HEADERS "*.h")
set(USER_PRECOMPILED_HEADER "")
foreach(header ${HEADERS})
    string (REPLACE "${CMAKE_SOURCE_DIR}/" "" header "${header}")
    list(APPEND USER_PRECOMPILED_HEADER "#include <${header}>")
endforeach()
string (REPLACE ";" "\n" USER_PRECOMPILED_HEADER "${USER_PRECOMPILED_HEADER}")
configure_file(${FISCO_BCOS_CMAKE_DIR}/templates/UserPrecompiled.h.in ${CMAKE_BINARY_DIR}/include/UserPrecompiled.h @ONLY)