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

include(Version)
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

file(GLOB_RECURSE SRCS bcos-pbft/*.cpp)

find_package(Protobuf CONFIG REQUIRED)

add_library(${PBFT_TARGET} ${SRCS} ${MESSAGES_SRCS})
target_link_libraries(${PBFT_TARGET} PUBLIC ${UTILITIES_TARGET} ${TOOL_TARGET})

if (TESTS)
    # fetch bcos-test    
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()