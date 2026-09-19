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

# WI-E2: verbatim op-geth (e8800cffe) eth_getTransactionReceipt goldens captured from the
# corpus devnet (Web3ResponseTest.cpp block comment; capture recipe in the corpus
# tools/devnet/README.md §WI-E2).
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE
    WEB3_RPC_OPGETH_DEPOSIT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/unittests/rpc/golden/op-geth-deposit-receipt")

# transaction-scheduler: EthGetProofIntegrationTest.cpp drives the FullChainFixture
# end-to-end harness (transaction-scheduler/tests/FullChainFixture.h); it brings
# ledger/storage (RocksDB) transitively. Deliberately NOT linking `executor` here even
# though the BaselineScheduler instantiation references bcos::unhexAddress: executor
# PUBLIC-links wedprcrypto's Rust static libraries, whose bundled runtime breaks libc++
# exception catching binary-wide (testCallRequest/decode_invalid_gas_string starts
# reporting "unknown type" for a plain std::invalid_argument). The test file carries a
# linkage-only definition of unhexAddress instead.
target_link_libraries(${TEST_BINARY_NAME} txpool mempool ledger rpc tool transaction-scheduler storage Boost::unit_test_framework)
set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("unittests/rpc/RpcReadHandlersTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# FullChainFixture.h pulls RocksDB and testutils headers whose names collide with the
# using-directives other unity-merged TUs carry; compile its user standalone.
set_source_files_properties(
    "unittests/rpc/EthGetProofIntegrationTest.cpp" "unittests/rpc/FullChainFixtureInst.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# RPCFixture.h pulls in the same heavy headers; keep the M3 receipt baseline standalone
# so its using-directives cannot leak into unity-merged TUs.
set_source_files_properties(
    "unittests/rpc/ReceiptFieldBaselineTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
#add_test(NAME ${TEST_BINARY_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")
enable_testing()