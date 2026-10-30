#------------------------------------------------------------------------------
# Top-level CMake file for bcos-network
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
#------------------------------------------------------------------------------

# bcos-network: the standalone generic framed-transport library, extracted from
# bcos-gateway/bcos-gateway/libnetwork (which now holds only backward-compatibility shim
# headers). Deliberately free of any bcos-framework dependency so it can serve any framed
# protocol (gateway P2P today, devp2p RLPx next).

find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(OpenSSL REQUIRED)
find_package(range-v3 REQUIRED)

# The four sources are unity-built under the repo-wide CMAKE_UNITY_BUILD like every other
# module; keep them self-contained (no file-scope `using namespace std;` — it leaks into the
# other TUs in the batch and collides with range-v3's `ranges`).
add_library(bcos-network
    bcos-network/ASIOInterface.cpp
    bcos-network/Common.cpp
    bcos-network/Host.cpp
    bcos-network/Session.cpp)
target_include_directories(bcos-network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-network>)
# PUBLIC: the public headers include bcos-task/bcos-utilities/range-v3/Boost headers.
target_link_libraries(bcos-network PUBLIC bcos-utilities bcos-task OpenSSL::SSL OpenSSL::Crypto range-v3::range-v3 Boost::filesystem)

# ut — standalone: the suite links only this library, so it runs in configurations without the
# gateway (FULLNODE=OFF + TESTS=ON included).
if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-network EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-network" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
