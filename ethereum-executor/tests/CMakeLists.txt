# Standalone EEST runner (no Boost.Test dependency, uses TBB for parallelism)
add_executable(eest-runner EESTStandalone.cpp)
target_link_libraries(eest-runner
    ethereum-executor bcos-evm-eth executor ledger protocol-tars bcos-framework rpc
    evmone::evmone jsoncpp_static TBB::tbb)
add_dependencies(eest-runner executor)
set_source_files_properties("EESTStandalone.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(eest-runner PROPERTIES UNITY_BUILD OFF)
set_target_properties(eest-runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
