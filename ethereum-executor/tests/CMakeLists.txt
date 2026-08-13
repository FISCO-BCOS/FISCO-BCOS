# Standalone EEST runner (no Boost.Test dependency, uses TBB for parallelism)
find_package(jsoncpp CONFIG REQUIRED)
add_executable(eest-runner EESTRunner.cpp)
target_link_libraries(eest-runner
    ethereum-executor bcos-evm-eth executor ledger protocol-tars bcos-framework rpc
    evmone::evmone jsoncpp_static TBB::tbb)
add_dependencies(eest-runner executor)
set_source_files_properties("EESTRunner.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(eest-runner PROPERTIES UNITY_BUILD OFF)
set_target_properties(eest-runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Compile/instantiation smoke coverage for the split-2/4 orphan headers
# (EVMSupport.h / EthereumState.h): nothing else includes them yet, so this
# target is the first place a syntax, template-instantiation or ODR error in
# those headers shows up.
add_executable(test-ethereum-state-smoke TestEthereumStateSmoke.cpp)
target_link_libraries(test-ethereum-state-smoke
    ethereum-executor bcos-framework bcos-task codec bcos-utilities
    evmone::evmone intx::intx TBB::tbb)
set_source_files_properties("TestEthereumStateSmoke.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-ethereum-state-smoke PROPERTIES UNITY_BUILD OFF)
set_target_properties(test-ethereum-state-smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
# SearchTestCases.cmake only discovers BOOST_*TEST_* targets; this standalone
# target must register itself so ctest actually runs it in CI.
add_test(NAME test-ethereum-state-smoke COMMAND test-ethereum-state-smoke)

# OP Stack execution lane: 0x7E decoding, executeDeposit, and the
# feature_l2_ethereum_compat dispatch onto opTransition.
add_executable(test-opstack-execution TestOpStackExecution.cpp)
target_link_libraries(test-opstack-execution
    ethereum-executor protocol-tars bcos-framework bcos-task codec bcos-utilities
    evmone::evmone intx::intx TBB::tbb)
set_source_files_properties("TestOpStackExecution.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-opstack-execution PROPERTIES UNITY_BUILD OFF)
set_target_properties(test-opstack-execution PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
add_test(NAME test-opstack-execution COMMAND test-opstack-execution)
