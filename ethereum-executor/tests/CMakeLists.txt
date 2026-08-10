# EEST fixtures download; exposes EVM_REF_EEST_ROOT (CACHE INTERNAL).
# 注：按计划 Step 4 备选走了最小裁剪——eth-eest-test 仅含下载机制
# （FetchReferenceAssets.cmake + upstream-pins.json），不含 runner 目标
# （其 src/runners 依赖 worktree 版 bcos-evm API，主树不可编译）。
# EEST_FIXTURE_DIR=${EVM_REF_EEST_ROOT}/fixtures（直接含 state_tests/ 等）。
add_subdirectory(../../bcos-evm/test/eth-eest-test bcos-evm-eest-assets)

# Standalone EEST runner (no Boost.Test dependency, uses TBB for parallelism)
find_package(jsoncpp CONFIG REQUIRED)
add_executable(eest-runner EESTRunner.cpp EestFailuresJson.cpp)
# bcosevm::opstack MUST precede bcos-evm-eth: the opstack static archive references eth symbols
# (OpTransition/OpHost resolve against the vendored eth state), and on mac static linking the
# defining archive must follow the referencing one.
target_link_libraries(eest-runner
    ethereum-executor bcosevm::opstack bcos-evm-eth executor ledger protocol-tars bcos-framework rpc
    evmone::evmone jsoncpp_static TBB::tbb)
add_dependencies(eest-runner executor)
set_source_files_properties("EESTRunner.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(eest-runner PROPERTIES UNITY_BUILD OFF)
set_target_properties(eest-runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Standalone unit tests for EestFailuresJson (writeFailuresJson + firstMismatchCategory).
# Kept independent: bcos-evm-opstack-tests does not link jsoncpp_static and
# reverse-depending on ethereum-executor would be circular.
find_package(Boost REQUIRED COMPONENTS unit_test_framework)
add_executable(eest-json-tests
    EestFailuresJsonTest.cpp
    EestFailuresJson.cpp
    EestSpikeForkTest.cpp)
target_link_libraries(eest-json-tests PRIVATE jsoncpp_static Boost::unit_test_framework)
set_source_files_properties("EestFailuresJsonTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(eest-json-tests PROPERTIES UNITY_BUILD OFF)
set_target_properties(eest-json-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# ---------------------------------------------------------------------------
# PR smoke subset (Task 7): register eest-runner as a ctest so a PR that
# regresses the EVM turns red automatically.
#
# The committed baseline (bcos-evm/test/eth-eest-test/assets/
# baseline-fails-v540.json) is [] — the full suite passes 100% — so the smoke
# subset is a small REPRESENTATIVE set of PASSING fixtures (one per fork /
# feature), NOT the result of subtracting known failures.  It is deterministic
# (fixed paths), never randomly sampled, and capped well under the 20-file
# budget for ctest speed (12 files / 473 test executions, ~0.2 s).
#
# eest-runner only honors a single --fixture (std::string singleFixture), so a
# multi-file smoke subset is materialised as a directory of symlinks into the
# fetched fixture tree and run with --fixture-dir (recursive, file-symlink
# safe).  The directory + manifest are regenerated at configure time, which
# re-runs whenever this list changes.
# ---------------------------------------------------------------------------
set(EEST_PR_SMOKE_SUBSET
    "state_tests/prague/eip7702_set_code_tx/test_address_from_set_code.json"
    "state_tests/cancun/eip1153_tstore/test_basic_tload_after_store.json"
    "state_tests/cancun/eip5656_mcopy/test_mcopy_on_empty_memory.json"
    "state_tests/cancun/eip4844_blobs/test_blob_tx_attribute_opcodes.json"
    "state_tests/shanghai/eip3855_push0/test_push0_contracts.json"
    "state_tests/shanghai/eip3860_initcode/test_contract_creating_tx.json"
    "state_tests/london/eip1559_fee_market_change/test_eip1559_tx_validity.json"
    "state_tests/paris/eip7610_create_collision/test_init_collision_create_tx.json"
    "state_tests/berlin/eip2930_access_list/test_eip2930_tx_validity.json"
    "state_tests/istanbul/eip152_blake2/test_blake2b.json"
    "state_tests/frontier/opcodes/test_all_opcodes.json"
    "state_tests/static/state_tests/stExample/add11.json")

set(_eest_smoke_dir "${CMAKE_BINARY_DIR}/eest-pr-smoke")
file(REMOVE_RECURSE "${_eest_smoke_dir}")
file(MAKE_DIRECTORY "${_eest_smoke_dir}")
set(_eest_smoke_manifest "${_eest_smoke_dir}/manifest.txt")
file(WRITE "${_eest_smoke_manifest}" "EEST_ROOT=${EVM_REF_EEST_ROOT}/fixtures\nSMOKE_DIR=${_eest_smoke_dir}\n")
set(_eest_smoke_idx 0)
foreach(_rel IN LISTS EEST_PR_SMOKE_SUBSET)
    math(EXPR _eest_smoke_idx "${_eest_smoke_idx} + 1")
    get_filename_component(_eest_dir "${_rel}" DIRECTORY)
    get_filename_component(_eest_base "${_rel}" NAME)
    string(REGEX REPLACE "/" "_" _eest_flat "${_eest_dir}")
    if(_eest_smoke_idx LESS 10)
        set(_eest_prefix "0${_eest_smoke_idx}")
    else()
        set(_eest_prefix "${_eest_smoke_idx}")
    endif()
    set(_eest_link "${_eest_smoke_dir}/${_eest_prefix}_${_eest_flat}_${_eest_base}")
    set(_eest_src "${EVM_REF_EEST_ROOT}/fixtures/${_rel}")
    if(NOT EXISTS "${_eest_src}")
        message(FATAL_ERROR "EEST PR smoke fixture missing: ${_eest_src}")
    endif()
    if(NOT EXISTS "${_eest_link}")
        file(CREATE_LINK "${_eest_src}" "${_eest_link}" SYMBOLIC)
    endif()
    file(APPEND "${_eest_smoke_manifest}" "FIXTURE=${_eest_link}\n")
endforeach()
unset(_eest_smoke_manifest)
unset(_eest_smoke_idx)
unset(_eest_dir)
unset(_eest_base)
unset(_eest_flat)
unset(_eest_prefix)
unset(_eest_link)
unset(_eest_src)

# ctest: `ctest -R EestRunner` runs the smoke subset in baseline mode and fails
# red on any regression (eest-runner exits 1 when g_totalFailed > 0).  The
# JSON failures are also left in the build dir for inspection / the regression
# script.  $<TARGET_FILE:> makes the test independent of PATH/working dir.
add_test(NAME EestRunner
    COMMAND $<TARGET_FILE:eest-runner>
        --fixture-dir "${_eest_smoke_dir}"
        --json-failures "${CMAKE_BINARY_DIR}/pr-smoke-fails.json")
set_tests_properties(EestRunner PROPERTIES
    ENVIRONMENT "EEST_FIXTURE_DIR=${EVM_REF_EEST_ROOT}/fixtures")
