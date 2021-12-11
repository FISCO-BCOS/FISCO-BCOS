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
set(PROTO_INPUT_PATH ${CMAKE_SOURCE_DIR}/bcos-pbft)
set(PROTO_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/)

set(MESSAGES_PROTOS bcos-pbft/core/proto/Consensus.proto bcos-pbft/pbft/protocol/proto/PBFT.proto)
foreach(proto_file ${MESSAGES_PROTOS})
    get_filename_component(bcos_proto_abs "${PROTO_INPUT_PATH}" ABSOLUTE)
    set(proto_file_abs ${bcos_proto_abs}/${proto_file})
    get_filename_component(rel_dir ${proto_file} DIRECTORY)
    get_filename_component(basename ${proto_file} NAME_WE)
    set(generated_files ${PROTO_OUTPUT_PATH}/${basename}.pb.cc ${PROTO_OUTPUT_PATH}/${basename}.pb.h)

    list(APPEND MESSAGES_SRCS ${PROTO_OUTPUT_PATH}/${rel_dir}/${basename}.pb.cc)

    message("Command: protoc --cpp_out ${PROTO_OUTPUT_PATH} -I ${PROTO_INPUT_PATH} ${proto_file}")
    add_custom_command(
        OUTPUT ${generated_files}
        COMMAND protobuf::protoc --cpp_out ${PROTO_OUTPUT_PATH} -I ${PROTO_INPUT_PATH} ${proto_file}
        COMMENT "Generating ${generated_files} from ${proto_file_abs}"
        VERBATIM
    )
endforeach()

include_directories(${CMAKE_SOURCE_DIR}/bcos-pbft)
include_directories(${PROTO_OUTPUT_PATH})
include_directories(${CMAKE_SOURCE_DIR}/)

aux_source_directory(bcos-pbft/core SRCS)
aux_source_directory(bcos-pbft/pbft SRCS)
aux_source_directory(bcos-pbft/pbft/config SRC_LIST)
aux_source_directory("bcos-pbft/pbft/cache" SRC_LIST)
aux_source_directory(bcos-pbft/pbft/engine SRC_LIST)
aux_source_directory(bcos-pbft/pbft/protocol SRC_LIST)
aux_source_directory(bcos-pbft/pbft/protocol/PB SRC_LIST)
aux_source_directory(bcos-pbft/pbft/storage SRC_LIST)

find_package(Protobuf CONFIG REQUIRED)

add_library(pbft ${SRCS} ${MESSAGES_SRCS})
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
