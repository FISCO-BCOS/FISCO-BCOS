vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/context
    REF boost-1.88.0
    SHA512 ce616623d95e0c394a68e962c52b11875a216af10597fcc3a2bf3fb7b13dcc47fa7f5ebf37c9bcf2fc8b2edacbf82d149b32f55cfda4c41bdca18fe3c3337b50
    HEAD_REF master
    PATCHES
        marmasm.patch
)

if(VCPKG_TARGET_IS_LINUX)
    message(STATUS "Linux detected, using ucontext for Boost.Context")
    set(FEATURE_OPTIONS "-DBOOST_CONTEXT_IMPLEMENTATION=ucontext")
else()
    set(FEATURE_OPTIONS "")
endif()
boost_configure_and_install(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS}
)
