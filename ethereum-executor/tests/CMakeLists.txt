# Standalone EEST runner (no Boost.Test dependency, uses TBB for parallelism)
find_package(jsoncpp CONFIG REQUIRED)
add_executable(eest-runner EESTRunner.cpp)
target_link_libraries(eest-runner
    ethereum-executor evmone::state executor ledger protocol-tars bcos-framework rpc
    evmone::evmone jsoncpp_static TBB::tbb)
add_dependencies(eest-runner executor)
set_source_files_properties("EESTRunner.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(eest-runner PROPERTIES UNITY_BUILD OFF)
set_target_properties(eest-runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
