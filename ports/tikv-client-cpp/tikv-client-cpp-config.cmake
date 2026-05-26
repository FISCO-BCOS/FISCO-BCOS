# tikv-client-cpp-config.cmake
# Creates the plain 'kv_client' imported target
# (compatible with the original ExternalProject target name)

find_package(OpenSSL REQUIRED)

set(KVCLIENT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
set(KVCLIENT_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib")

# Determine build mode
get_filename_component(SHARE_DIR "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
if(SHARE_DIR MATCHES "/debug/")
    set(TIKV_BUILD_MODE "debug")
else()
    set(TIKV_BUILD_MODE "release")
endif()

if(NOT TARGET kv_client)
    add_library(kv_client INTERFACE IMPORTED)
    set_target_properties(kv_client PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${KVCLIENT_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${KVCLIENT_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}tikvcpp${CMAKE_STATIC_LIBRARY_SUFFIX};${KVCLIENT_LIB_DIR}/${TIKV_BUILD_MODE}/${CMAKE_STATIC_LIBRARY_PREFIX}tikvrust${CMAKE_STATIC_LIBRARY_SUFFIX};OpenSSL::SSL;OpenSSL::Crypto"
    )
endif()
