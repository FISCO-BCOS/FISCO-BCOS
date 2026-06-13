#------------------------------------------------------------------------------
# Top-level CMake file for bcos-rlp-protocol
# ------------------------------------------------------------------------------
# Copyright (C) 2024 FISCO BCOS.
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

file(GLOB SRC_LIST "bcos-rlp-protocol/*.cpp")
file(GLOB HEADERS "bcos-rlp-protocol/*.h")

add_library(bcos-rlp-protocol ${SRC_LIST} ${HEADERS})
target_include_directories(bcos-rlp-protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-rlp-protocol>
)
target_link_libraries(bcos-rlp-protocol PUBLIC
    bcos-framework
    codec
    bcos-crypto
    bcos-utilities
)

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE True)
    add_subdirectory(test)
endif()
