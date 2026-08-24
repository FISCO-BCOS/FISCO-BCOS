#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-framework
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
file(GLOB_RECURSE SOURCES "*.cpp" "*.h" "*.sol")

# cmake settings
set(TEST_BINARY_NAME test-bcos-pbft)
find_package(Protobuf REQUIRED)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} pbft rpbft tool ledger table bcos-crypto protocol-tars protobuf::libprotobuf Boost::unit_test_framework)
set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
#add_test(NAME test-pbft WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
config_test_cases("" "${SOURCES}" "${TEST_BINARY_NAME}" "" "")

# Protobuf v5.29.x ABSL_ANNOTATE_CONTIGUOUS_CONTAINER false positives:
# disable detect_container_overflow only for tests that go through
# RepeatedField SOO / ParseFromArray code paths.
disable_container_overflow_for_tests(
    FIB120_PBFTProposalDecodeBounds/decode_rejects_excessive_signatureList
    FIB120_PBFTProposalDecodeBounds/decode_accepts_normal_signatureList
    FIB120_PBFTProposalDecodeBounds/decode_rejects_excessive_nodeList_FIB123
    FIB120_PBFTProposalDecodeBounds/decode_rejects_signatureList_larger_than_nodeList_FIB120
    FIB120_PBFTProposalDecodeBounds/decode_rejects_signatureList_smaller_than_nodeList_FIB120
    FIB121ProofSurvivalTest/mutateAfterSetReflectedAtEncode
    FIB121ProofSurvivalTest/proofSurvivesViewChangeEmbedding
    FIB122_PBFTProposalSubmessageOwnership/decode_then_decode_no_double_free
    FIB122_PBFTProposalSubmessageOwnership/decode_followed_by_proposal_access
    FIB124NewViewPrePrepareCrosscheckTest/testViewChangeFLowWithPrecommitProposals
    FIB127LogRecoverySigCheckTest/testDropProposalWithInsufficientWeight
    FIB127LogRecoverySigCheckTest/testDropProposalWithDuplicateSealerProofs
    FIB129ResetTxsFlagPostVerifyTest/testPrePrepareRejectedOnVerifyException
    FIB129ResetTxsFlagPostVerifyTest/testPrePrepareRejectedOnVerifyFail
    FIB134_PacketTypeSignature/v1_inner_packetType_tampered_rejected
    FIB134_PacketTypeSignature/v1_outer_packetType_tampered_rejected
    FIB134_PacketTypeSignature/default_version_binds_outer_packetType
    FIB134_PacketTypeSignature/v0_legacy_message_accepted
    FIB134_PacketTypeSignature/dual_mode_receiver_v0_and_v1_both_verify
    PBFTConfigTest/testPBFTInit
    PBFTViewChangeTest/testViewChangeWithPrecommitProposals
    PBFTMessageTest/testNormalPBFTMessage
    PBFTMessageTest/testSMPBFTMessage
    PBFTMessageTest/testNormalViewChangeMessage
    PBFTMessageTest/testSMViewChangeMessage
    PBFTMessageTest/testNormalNewViewMessage
    PBFTMessageTest/testSMNewViewMessage
)