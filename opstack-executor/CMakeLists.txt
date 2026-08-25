cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(opstack-executor
    OpBlockExecute.cpp
)
target_include_directories(opstack-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/opstack-executor>)
# No direct `ledger` / `rlp-protocol` links: both drag the bcos-crypto→wedprcrypto chain
# that breaks libc++ typed catch binary-wide (bcos-rpc/test/CMakeLists.txt:29-34).
# `ledger` still arrives transitively via bcos-evm-eth. ①a's new headers (mpt/MPTBuilder.h,
# transaction-scheduler/HistoricalCallStorage.h) follow the same rule — include paths and
# buildAndCollect symbols arrive transitively (SchedulerSerialImpl.h precedent).
target_link_libraries(opstack-executor PUBLIC
    bcos-evm-opstack ethereum-executor bcos-framework bcos-protocol
    evmone::evmone intx::intx TBB::tbb)
set_target_properties(opstack-executor PROPERTIES UNITY_BUILD OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # Designated initializers and GCC-13 jAt refs trip -Werror here only.
    target_compile_options(opstack-executor PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
