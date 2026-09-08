#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-engine
# ------------------------------------------------------------------------------

file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h")
include(SearchTestCases)

set(TEST_BINARY_NAME test-bcos-engine)

# Finding BW: leftover EngineServiceImpl.cpp is no longer linked. Tests that still
# include EngineServiceImpl.h instantiate the header-only template (BK stays there
# until those fixtures migrate off leftover).
add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE
    . ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/opstack-executor/tests
    ${CMAKE_SOURCE_DIR}/bcos-rpc)

find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC
    engine mempool protocol-tars rlp-protocol opstack-executor ledger rpc transaction-scheduler
    jsoncpp_static Boost::unit_test_framework)

# S6: GoldenSample / t8n corpus. The vectors and golden/engine/ files are tracked in-tree
# under opstack-executor/tests/t8n/ (the external FISCO-BCOS/op-stack-e2e-tests pin is an
# optional refresh source, not a build requirement); OP_T8N_* below only override the
# location. Local builds skip when the corpus is absent, CI (GITHUB_ACTIONS/CI or
# -DREQUIRE_T8N_CORPUS=ON) must fail instead.
if(NOT DEFINED OP_T8N_VECTORS_DIR)
    set(OP_T8N_VECTORS_DIR "${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/vectors")
endif()
if(NOT DEFINED OP_T8N_GOLDEN_ENGINE_DIR)
    set(OP_T8N_GOLDEN_ENGINE_DIR
        "${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/golden/engine")
endif()
set(_t8n_defs
    OP_T8N_VECTORS_DIR="${OP_T8N_VECTORS_DIR}"
    OP_T8N_GOLDEN_ENGINE_DIR="${OP_T8N_GOLDEN_ENGINE_DIR}")
if(REQUIRE_T8N_CORPUS OR DEFINED ENV{GITHUB_ACTIONS} OR DEFINED ENV{CI})
    list(APPEND _t8n_defs FISCO_REQUIRE_T8N_CORPUS=1)
endif()
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE ${_t8n_defs})

set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/OpEngineServiceParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/OpEngineServiceExecParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/OpEngineReviewFixTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceMPTStateRootTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceTransactionalTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")
