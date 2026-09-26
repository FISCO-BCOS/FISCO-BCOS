#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-leader-election
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
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
file(GLOB_RECURSE SOURCES "*.cpp")

# cmake settings
set(TEST_BINARY_NAME test-bcos-leader-election)

# FIB-168 / FIB-169: the test target builds the audit-touched WatcherConfig
# translation unit directly together with the test sources, instead of linking
# the full ${LEADER_ELECTION_TARGET}. This keeps the unit tests buildable on
# environments where unrelated translation units in bcos-leader-election (e.g.
# LeaderElection.cpp's etcdv3::Transaction usage) cannot be compiled against
# the locally-resolved etcd-cpp-apiv3 version. The audit fixes themselves only
# touch WatcherConfig.{h,cpp} (FIB-168) and LeaderElection.cpp (FIB-169 — the
# UT for which is fully self-contained and does not link the production file).
set(LEADER_ELECTION_TEST_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/WatcherConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ElectionConfig.cpp)

add_executable(${TEST_BINARY_NAME} ${SOURCES} ${LEADER_ELECTION_TEST_SRCS})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ../src ${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED unit_test_framework)
find_package(etcd-cpp-api CONFIG REQUIRED)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC bcos-utilities bcos-framework etcd-cpp-api
    Boost::unit_test_framework)

# FIB-168/169: the test target is new; UNITY_BUILD is intentionally NOT enabled
# so per-FIB test files do not need FIB-suffixed helpers.

include(SearchTestCases)
config_test_cases("" "${SOURCES}" "${TEST_BINARY_NAME}" "" "")
