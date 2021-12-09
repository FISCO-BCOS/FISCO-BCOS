/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file FileUtility.h
 */

#pragma once

#include "Common.h"
#include <boost/filesystem.hpp>
namespace bcos
{
/**
 * @brief : read the specified file
 *
 * @param _file : the file to be read
 * @return std::shared_ptr<bytes> : the file content
 *         If the file doesn't exist or isn't readable,
 *         returns an empty bytes.
 */
std::shared_ptr<bytes> readContents(boost::filesystem::path const& _file);

/**
 * @brief : read the content of the specified file, and return the content as string
 *
 * @param _file : the file
 * @return std::shared_ptr<std::string> : the content of the specified file
 *         If the file doesn't exist or isn't readable,
 *         returns an empty string
 */
std::shared_ptr<std::string> readContentsToString(boost::filesystem::path const& _file);
}  // namespace bcos
