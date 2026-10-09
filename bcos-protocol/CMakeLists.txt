#------------------------------------------------------------------------------
# Top-level CMake file for bcos-protocol
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


add_subdirectory(bcos-protocol)

if(WITH_CXX_MODULES)
    # Named-module build: the module interface unit is compiled once into this
    # static library; consumers get the BMI on their compile line by linking
    # bcos-protocol (and the FISCO_WITH_CXX_MODULES define that switches their
    # guarded `import bcos.protocol;` on).
    add_library(bcos-protocol STATIC)
    target_sources(bcos-protocol PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES bcos-protocol/Protocol.cppm)
    target_include_directories(bcos-protocol PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/bcos-protocol>
    )
    target_link_libraries(bcos-protocol PUBLIC codec)
    # Build-time-only dependencies of the module interface unit's global module
    # fragment (TopicItem.h pulls in bcos-framework/Common.h and json/json.h).
    find_package(jsoncpp CONFIG REQUIRED)
    target_link_libraries(bcos-protocol PRIVATE bcos-framework JsonCpp::JsonCpp)
else()
    add_library(bcos-protocol INTERFACE)
    target_include_directories(bcos-protocol INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/bcos-protocol>
    )
    target_link_libraries(bcos-protocol INTERFACE codec)
endif()

if (TESTS)
    # fetch bcos-test
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
if(WITH_CXX_MODULES)
    install(TARGETS bcos-protocol EXPORT fiscobcosTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
else()
    install(TARGETS bcos-protocol EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
install(DIRECTORY "bcos-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")