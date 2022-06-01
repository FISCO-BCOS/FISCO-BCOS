#------------------------------------------------------------------------------
# Top-level CMake file for bcos-sync
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-sync
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
project(bcos-sync VERSION ${VERSION})

# proto generation
set(PROTO_INPUT_PATH ${CMAKE_SOURCE_DIR}/bcos-sync)
set(PROTO_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/)

set(SYNC_PROTOS bcos-sync/protocol/proto/BlockSync.proto)
foreach(proto_file ${SYNC_PROTOS})
    get_filename_component(bcos_proto_abs "${PROTO_INPUT_PATH}" ABSOLUTE)
    set(proto_file_abs ${bcos_proto_abs}/${proto_file})
    get_filename_component(rel_dir ${proto_file} DIRECTORY)
    get_filename_component(basename ${proto_file} NAME_WE)
    set(generated_files ${PROTO_OUTPUT_PATH}/${rel_dir}/${basename}.pb.cc)

    list(APPEND SYNC_SRCS ${generated_files})

    message("Command: protoc --cpp_out ${PROTO_OUTPUT_PATH} -I ${PROTO_INPUT_PATH} ${proto_file}")
    add_custom_command(
        OUTPUT ${generated_files}
        COMMAND protobuf::protoc --cpp_out ${PROTO_OUTPUT_PATH} -I ${PROTO_INPUT_PATH} ${proto_file}
        COMMENT "Generating ${generated_files} from ${proto_file_abs}"
        VERBATIM
    )
endforeach()

find_package(Protobuf CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

include_directories(${PROTO_OUTPUT_PATH})

file(GLOB_RECURSE SRCS bcos-sync/*.cpp)
add_library(${SYNC_TARGET} ${SRCS} ${SYNC_SRCS})
target_compile_options(${SYNC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${SYNC_TARGET} PUBLIC ${UTILITIES_TARGET} ${TOOL_TARGET} jsoncpp_static)


if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

# for doxygen
# include(BuildDocs)
# buildDoc(bcos-sync-doc)

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("sync-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
