#------------------------------------------------------------------------------
# bcos-ops: operator subcommands built into the fisco-bcos binary
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
file(GLOB_RECURSE SRC_LIST "bcos-ops/*.cpp")

find_package(jsoncpp CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

add_library(bcos-ops ${SRC_LIST})
target_include_directories(bcos-ops PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-ops PUBLIC bcos-cpp-sdk bcos-utilities jsoncpp_static ftxui::screen ftxui::dom ftxui::component)
set_target_properties(bcos-ops PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
