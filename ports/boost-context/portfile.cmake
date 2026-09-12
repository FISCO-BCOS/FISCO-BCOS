vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/context
    REF boost-1.90.0
    SHA512 f7251e7d8b4820b41b25b3f7386d6753de3a2da37397618df83aed96af500303b28aa993f46f11f77dc1d334a3bbaa14f9f63282f12c881bd68a6e5bcc3fad9c
    HEAD_REF master
)

if(VCPKG_TARGET_IS_LINUX)
    # Keep ucontext on Linux: with the default fcontext, exception unwinding
    # across coroutine2 fiber boundaries (boost::context::detail::fiber_unwind)
    # SEGVs under ASan — 88 executor tests fail (testTransactionExecutive,
    # TestDagExecutor, CompatExecutorSmoke, ...). Verified with boost 1.90.0.
    message(STATUS "Linux detected, using ucontext for Boost.Context")
    set(FEATURE_OPTIONS "-DBOOST_CONTEXT_IMPLEMENTATION=ucontext")
else()
    set(FEATURE_OPTIONS "")
endif()
boost_configure_and_install(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS}
)
