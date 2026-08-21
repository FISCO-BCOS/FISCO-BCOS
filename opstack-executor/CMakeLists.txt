cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(opstack-executor INTERFACE)
target_include_directories(opstack-executor INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:include/opstack-executor>)
# No direct `ledger` / `rlp-protocol` links: header-only usage only (Classify.h), and both drag
# the bcos-crypto→wedprcrypto chain that breaks libc++ typed catch binary-wide
# (bcos-rpc/test/CMakeLists.txt:29-34). `ledger` still arrives transitively via bcos-evm-eth.
target_link_libraries(opstack-executor INTERFACE
    bcos-evm-opstack ethereum-executor bcos-framework bcos-protocol
    evmone::evmone intx::intx TBB::tbb)
set_target_properties(opstack-executor PROPERTIES UNITY_BUILD OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # OP modules deliberately use skipped-field designated initializers and GCC-13
    # reference-returning helpers (jAt); downgrade only here, not repository-wide.
    # INTERFACE, not PRIVATE: opstack-executor is a header-only INTERFACE library — the
    # -Wno-error flags must reach the consumers' translation units, where the templates
    # are instantiated and the warnings fire.
    target_compile_options(opstack-executor INTERFACE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
