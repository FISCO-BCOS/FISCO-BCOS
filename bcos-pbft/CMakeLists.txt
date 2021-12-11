#------------------------------------------------------------------------------
# Top-level CMake file for bcos-pbft
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-pbft
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
#------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.10)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

project(bcos-pbft VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

# proto generation
set(PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(PROTO_GENERATE_BASE_DIR ${CMAKE_BINARY_DIR})
set(CONSENSUS_PROTO_SUB_DIR "bcos-pbft/core/proto")
set(PBFT_PROTO_SUB_DIR "bcos-pbft/pbft/protocol/proto")

include(GenerateSources)
set(CONSENSUS_PROTOS Consensus.proto)
set(CONSENSUS_PROTO_GENERATE_DIR ${PROTO_GENERATE_BASE_DIR}/${CONSENSUS_PROTO_SUB_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${CONSENSUS_PROTOS} ${PROTO_PATH} "protocol/proto" ${PROTO_GENERATE_BASE_DIR} ${CONSENSUS_PROTO_GENERATE_DIR})

set(PBFT_PROTOS PBFT.proto)
set(PBFT_PROTO_GENERATE_DIR ${PROTO_GENERATE_BASE_DIR}/${PBFT_PROTO_SUB_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PBFT_PROTOS} ${PROTO_PATH} "proto" ${PROTO_GENERATE_BASE_DIR} ${PBFT_PROTO_GENERATE_DIR})

aux_source_directory(bcos-pbft/core SRCS)
aux_source_directory(bcos-pbft/pbft SRCS)
aux_source_directory(bcos-pbft/pbft/config SRC_LIST)
aux_source_directory("bcos-pbft/pbft/cache" SRC_LIST)
aux_source_directory(bcos-pbft/pbft/engine SRC_LIST)
aux_source_directory(bcos-pbft/pbft/protocol SRC_LIST)
aux_source_directory(bcos-pbft/pbft/protocol/PB SRC_LIST)
aux_source_directory(bcos-pbft/pbft/storage SRC_LIST)

add_library(pbft ${SRCS} ${PROTO_SRCS})
# target_compile_options(pbft PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(pbft PUBLIC framework)

# if (TESTS)
#     # fetch bcos-test    
#     enable_testing()
#     set(CTEST_OUTPUT_ON_FAILURE TRUE)
#     add_subdirectory(test)
# endif()

# # for doxygen
# include(BuildDocs)
# buildDoc(bcos-pbft-doc)

# for code coverage
# if (COVERAGE)
#     include(Coverage)
#     config_coverage("pbft-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
# endif ()
