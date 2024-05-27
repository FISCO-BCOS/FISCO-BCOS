/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file EntryCache.h
 * @author: Jimmy Shi
 * @date 2023/2/16
 */
#pragma once
#include "Entry.h"
#include <tbb/concurrent_unordered_map.h>

namespace bcos::storage
{
using EntryCache = tbb::concurrent_unordered_map<std::string, std::optional<storage::Entry>>;
using EntryCachePtr = std::shared_ptr<EntryCache>;

}  // namespace bcos::storage