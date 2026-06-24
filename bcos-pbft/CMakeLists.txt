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
project(bcos-pbft VERSION ${VERSION})

# proto generation
set(PROTO_INPUT_PATH ${CMAKE_SOURCE_DIR}/bcos-pbft)
set(PROTO_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/)

set(MESSAGES_PROTOS bcos-pbft/core/proto/Consensus.proto bcos-pbft/pbft/protocol/proto/PBFT.proto)
foreach(proto_file ${MESSAGES_PROTOS})
    get_filename_component(bcos_proto_abs "${PROTO_INPUT_PATH}" ABSOLUTE)
    set(proto_file_abs ${bcos_proto_abs}/${proto_file})
    get_filename_component(rel_dir ${proto_file} DIRECTORY)
    get_filename_component(basename ${proto_file} NAME_WE)
    set(generated_files ${PROTO_OUTPUT_PATH}/${rel_dir}/${basename}.pb.cc)

    list(APPEND MESSAGES_SRCS ${generated_files})

    message("Command: protoc --cpp_out ${PROTO_OUTPUT_PATH} -I ${PROTO_INPUT_PATH} ${proto_file}")
    add_custom_command(
        OUTPUT ${generated_files}
        COMMAND protobuf::protoc --cpp_out ${PROTO_OUTPUT_PATH} -I ${PROTO_INPUT_PATH} ${proto_file}
        COMMENT "Generating ${generated_files} from ${proto_file_abs}"
        VERBATIM
    )
endforeach()

include_directories(${PROTO_OUTPUT_PATH})

find_package(Protobuf CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

file(GLOB_RECURSE SRCS bcos-pbft/*.cpp)
add_library(${PBFT_TARGET} ${SRCS} ${MESSAGES_SRCS})
# Export the generated-protobuf output dir to consumers: the PB message headers
# (pulled transitively via PBFTMessageFactory.h / PBFTConfig.h) include
# "bcos-pbft/pbft/protocol/proto/PBFT.pb.h", which lives under the build tree.
# Directory-scoped include_directories() above does not propagate past this
# subtree, so downstream targets (e.g. test-sealer) need it on the link interface
# -- mirrors bcos-txpool's PUBLIC export of its own generated headers.
target_include_directories(${PBFT_TARGET} PUBLIC ${PROTO_OUTPUT_PATH})
target_link_libraries(${PBFT_TARGET} PUBLIC ${TXPOOL_TARGET} ${LEDGER_TARGET} ${UTILITIES_TARGET} ${TOOL_TARGET} bcos-framework jsoncpp_static)
set_source_files_properties(
    "bcos-pbft/pbft/protocol/proto/PBFT.pb.cc"
    "bcos-pbft/core/proto/Consensus.pb.cc"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${PBFT_TARGET} PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    # fetch bcos-test
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()