#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
#Top - level CMake file for ut of bcos - engine
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h")
include(SearchTestCases)

set(TEST_BINARY_NAME test-bcos-engine)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE
    . ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/opstack-executor/tests
    ${CMAKE_SOURCE_DIR}/bcos-rpc)

find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC
    engine mempool protocol-tars rlp-protocol opstack-executor ledger rpc
    jsoncpp_static Boost::unit_test_framework)

#S6 : GoldenSample / t8n corpus.The corpus is external : provision
#FISCO - BCOS / op - stack - e2e - tests and place vectors / and golden / engine / under
#opstack - executor / tests / t8n / (or point OP_T8N_ * below at your checkout).When it
#is absent the S6 golden cases skip explicitly(t8nCorpusAvailable).
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE
    OP_T8N_VECTORS_DIR="${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/vectors"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/golden/engine")

set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceTransactionalTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/OpEngineServiceParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/OpEngineServiceExecParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/OpEngineReviewFixTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")
