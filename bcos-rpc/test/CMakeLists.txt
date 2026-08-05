#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-rpc
# ------------------------------------------------------------------------------
# Copyright (C) 2021 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h" "unittests/*.sol")

# cmake settings
set(TEST_BINARY_NAME test-bcos-rpc)

find_package(Boost REQUIRED unit_test_framework)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE .)
target_compile_options(${TEST_BINARY_NAME} PRIVATE -Wno-unused-variable)

# transaction-scheduler: EthGetProofIntegrationTest.cpp drives the FullChainFixture
# end-to-end harness (transaction-scheduler/tests/FullChainFixture.h); it brings
# ledger/storage (RocksDB) transitively. Deliberately NOT linking `executor` here even
# though the BaselineScheduler instantiation references bcos::unhexAddress: executor
# PUBLIC-links wedprcrypto's Rust static libraries, whose bundled runtime breaks libc++
# exception catching binary-wide (testCallRequest/decode_invalid_gas_string starts
# reporting "unknown type" for a plain std::invalid_argument). The test file carries a
# linkage-only definition of unhexAddress instead.
target_link_libraries(${TEST_BINARY_NAME} txpool ledger rpc tool transaction-scheduler storage Boost::unit_test_framework)
# OP Stack golden corpus (shared with bcos-evm's EthBlockHeaderTest): the deposit envelope
# encoder lives in bcos-rpc (DepositTxHandler), so OpDepositEncodeTest.cpp cross-checks it
# byte-for-byte against the op-geth golden rawTransactions here (in-tree build only, same as
# the corpus's bcos-evm users). Paths point into the bcos-evm test source tree.
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE
    OP_T8N_CASES_DIR="${CMAKE_SOURCE_DIR}/bcos-evm/test/opstack/t8n/cases"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_SOURCE_DIR}/bcos-evm/test/opstack/t8n/golden/engine")
set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("unittests/rpc/RpcReadHandlersTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# FullChainFixture.h pulls RocksDB and testutils headers whose names collide with the
# using-directives other unity-merged TUs carry; compile its user standalone.
set_source_files_properties(
    "unittests/rpc/EthGetProofIntegrationTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
#add_test(NAME ${TEST_BINARY_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")
enable_testing()