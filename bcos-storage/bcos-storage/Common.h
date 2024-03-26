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
 * @brief common functions
 * @file Common.h
 * @author: xingqiangbai
 * @date: 2021-10-11
 */

#pragma once

#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-utilities/ITTAPI.h>

namespace bcos::storage
{

extern const __itt_string_handle* const ITT_STRING_STORAGE_PREPARE;
extern const __itt_string_handle* const ITT_STRING_STORAGE_COMMIT;
extern const __itt_string_handle* const ITT_STRING_STORAGE_ROLLBACK;
extern const __itt_string_handle* const ITT_STRING_STORAGE_SET_ROWS;

inline bool isValid(const std::string_view& tableName)
{
    return !tableName.empty();
}

inline bool isValid(const std::string_view& tableName, const std::string_view&)
{
    return !tableName.empty();
}

}  // namespace bcos::storage
