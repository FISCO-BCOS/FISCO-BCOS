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
# File: InstallConfig.cmake
# Function: Public cmake files for building projects based on hunter
#           may be used by multiple projects
# ------------------------------------------------------------------------------

include(GNUInstallDirs)
set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Configuration

set(version_config "${generated_dir}/${PROJECT_NAME}ConfigVersion.cmake")
set(project_config "${generated_dir}/${PROJECT_NAME}Config.cmake")
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(namespace "${PROJECT_NAME}::")

# Include module with fuction 'write_basic_package_version_file'
include(CMakePackageConfigHelpers)

# Configure '<PROJECT-NAME>ConfigVersion.cmake'
write_basic_package_version_file(
    "${version_config}" COMPATIBILITY SameMajorVersion
)

set(CONFIGURE_TEMPLATE_FILE_PATH "${CMAKE_CURRENT_BINARY_DIR}/templates/Config.cmake.in")
set(CONFIGURE_TEMPLATE_FILE_CONTENT "@PACKAGE_INIT@ \n include(\"\${CMAKE_CURRENT_LIST_DIR}/@TARGETS_EXPORT_NAME@.cmake\") \n check_required_components(\"@PROJECT_NAME@\")")
file(WRITE "${CONFIGURE_TEMPLATE_FILE_PATH}" ${CONFIGURE_TEMPLATE_FILE_CONTENT})

configure_package_config_file(
    "${CONFIGURE_TEMPLATE_FILE_PATH}"
    "${project_config}"
    INSTALL_DESTINATION "${config_install_dir}"
)

install(
    FILES "${project_config}" "${version_config}"
    DESTINATION "${config_install_dir}"
)

install(
    EXPORT "${TARGETS_EXPORT_NAME}"
    NAMESPACE "${namespace}"
    DESTINATION "${config_install_dir}"
)
