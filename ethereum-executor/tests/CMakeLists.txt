# EEST fixtures download; exposes EVM_REF_EEST_ROOT (CACHE INTERNAL).
# 注：按计划 Step 4 备选走了最小裁剪——eth-eest-test 仅含下载机制
# （FetchReferenceAssets.cmake + upstream-pins.json），不含 runner 目标
# （其 src/runners 依赖 worktree 版 bcos-evm API，主树不可编译）。
# EEST_FIXTURE_DIR=${EVM_REF_EEST_ROOT}/fixtures（直接含 state_tests/ 等）。
add_subdirectory(../../bcos-evm/test/eth-eest-test bcos-evm-eest-assets)

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
