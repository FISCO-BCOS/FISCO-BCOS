#------------------------------------------------------------------------------
# Top-level CMake file for bcos-framework
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

cmake_minimum_required(VERSION 3.12)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

# fetch bcos-proto
set(BCOS_PROTO_NAME bcos-proto)
set(BCOS_PROTO_PATH ${CMAKE_CURRENT_BINARY_DIR}/libprotocol/${BCOS_PROTO_NAME})
include(FetchContent)
FetchContent_Declare(${BCOS_PROTO_NAME}
    GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-proto.git
    GIT_TAG 28e5620d348bd82dff649ae37ca25120d5a6223e
    SOURCE_DIR ${BCOS_PROTO_PATH}
    CMAKE_ARGS -DHUNTER_PACKAGE_LOG_BUILD=${HUNTER_PACKAGE_LOG_BUILD}
               -DHUNTER_PACKAGE_LOG_INSTALL=${HUNTER_PACKAGE_LOG_INSTALL}
               -DHUNTER_STATUS_DEBUG=${HUNTER_STATUS_DEBUG}
)
FetchContent_MakeAvailable(${BCOS_PROTO_NAME})

project(bcos-framework VERSION "3.0.0")

# define the generated proto file path
set(PROTO_GENERATE_DIR ${BCOS_PROTO_PATH})
set(PROTO_PATH ${PROJECT_SOURCE_DIR})

# generate sync protobuf files
set(SYNC_PROTO_GENERATE_BASE_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(SYNC_PROTO_SUB_DIR "libsync/protocol/proto")
set(SYNC_PROTO_GENERATE_DIR ${SYNC_PROTO_GENERATE_BASE_DIR}/${SYNC_PROTO_SUB_DIR})

set(MESSAGES_PROTOS Transaction.proto TransactionReceipt.proto BlockHeader.proto Block.proto)

foreach(proto_file ${MESSAGES_PROTOS})
  get_filename_component(bcos_proto_abs "${BCOS_PROTO_PATH}" ABSOLUTE)
  set(proto_file_abs ${bcos_proto_abs}/${proto_file})
  get_filename_component(basename ${proto_file} NAME_WE)
  set(generated_files ${PROTO_GENERATE_DIR}/${basename}.pb.cc ${PROTO_GENERATE_DIR}/${basename}.pb.h)

  list(APPEND MESSAGES_SRCS ${PROTO_GENERATE_DIR}/${basename}.pb.cc)
  list(APPEND MESSAGES_HDRS ${PROTO_GENERATE_DIR}/${basename}.pb.h)

  add_custom_command(
      OUTPUT ${generated_files}
      COMMAND protobuf::protoc --cpp_out ${PROTO_GENERATE_DIR} -I ${bcos_proto_abs} ${proto_file_abs}
      COMMENT "Generating ${generated_files} from ${proto_file_abs}"
      VERBATIM
  )
endforeach()

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# add subdirectories to build the libraries
aux_source_directory(libutilities SRCS)
aux_source_directory(libcodec SRCS)
aux_source_directory(libstorage SRCS)
aux_source_directory(libprotocol SRCS)
aux_source_directory(libprotocol/protobuf SRCS)
aux_source_directory(libsync SRCS)
aux_source_directory(libsealer SRCS)
aux_source_directory(libtool SRCS)

find_package(TBB CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED log chrono system filesystem iostreams thread)
find_package(Protobuf REQUIRED)

add_library(framework ${SRCS} ${MESSAGES_SRCS})
target_link_libraries(framework PUBLIC Boost::log Boost::chrono Boost::iostreams Boost::filesystem Boost::thread protobuf::libprotobuf TBB::tbb)

# test releated
# if (TESTS)
#     hunter_add_package(jsoncpp)
#     find_package(jsoncpp CONFIG REQUIRED)
#     enable_testing()
#     set(CTEST_OUTPUT_ON_FAILURE True)
#     add_subdirectory(test)
# endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("framework-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
