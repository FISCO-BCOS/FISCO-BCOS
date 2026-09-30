# Standalone EEST runner (no Boost.Test dependency, uses TBB for parallelism)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost REQUIRED unit_test_framework)
add_executable(eest-runner EESTRunner.cpp)
target_link_libraries(eest-runner
    ethereum-executor executor ledger protocol-tars bcos-framework rpc
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

# EIP-7702 authorization-list unit tests (ported from the retired
# bcos-evm/test/eth/EthTransitionTest.cpp in step 4.2 — the cross-chain
# authorization rejection pin).
add_executable(test-ethereum-authorization-list
    TestMain.cpp
    TestEthereumAuthorizationList.cpp)
target_include_directories(test-ethereum-authorization-list PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test-ethereum-authorization-list PRIVATE
    ethereum-executor protocol-tars bcos-framework bcos-task bcos-crypto codec
    evmone::evmone Boost::unit_test_framework)
set_target_properties(test-ethereum-authorization-list PROPERTIES UNITY_BUILD OFF)
add_test(NAME test-ethereum-authorization-list COMMAND test-ethereum-authorization-list)
