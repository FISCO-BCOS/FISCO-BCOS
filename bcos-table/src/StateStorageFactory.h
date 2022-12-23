/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief interface of Table
 * @file CacheStorageFactory.h
 * @author: wenlinli
 * @date: 2022-12-14
 */

#pragma once

#include "StateStorageInterface.h"
#include <bcos-framework/consensus/ConsensusNodeInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <tbb/concurrent_unordered_map.h>
#include <map>


namespace bcos::storage
{

// FileSystem paths
static const char* const FS_ROOT = "/";
static const char* const FS_APPS = "/apps";
static const char* const FS_USER = "/usr";
static const char* const FS_SYS_BIN = "/sys";
static const char* const FS_USER_TABLE = "/tables";

class StateStorageFactory
{
public:
    using Ptr = std::shared_ptr<StateStorageFactory>;
    StateStorageFactory(size_t keyPageSize) : m_keyPageSize(keyPageSize)
    {}

    virtual ~StateStorageFactory() = default;

    virtual storage::StateStorageInterface::Ptr createStateStorage(
        bcos::storage::StorageInterface::Ptr storage, uint32_t blockVersion);


private:
     size_t m_keyPageSize;

};

}
