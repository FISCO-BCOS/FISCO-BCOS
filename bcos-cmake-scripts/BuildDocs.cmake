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
# File: BuildDocs.cmake
# Function: Documentation build cmake
# ------------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    # Requirements: doxygen graphviz
      set(doxyfile_in "${CMAKE_CURRENT_LIST_DIR}/.Doxyfile.in")
      set(doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
      configure_file(${doxyfile_in} ${doxyfile} @ONLY)
elseif()
    message(WARNING "Doxygen is needed to build the documentation. Please install doxygen and graphviz")
endif()
function(buildDoc TARGET)
  if(DOXYGEN_FOUND)
    # Add doc target
    add_custom_target(${TARGET} COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
                          WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                          COMMENT "Generating documentation with Doxygen..." VERBATIM)
  endif()
endfunction()