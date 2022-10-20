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
 * @brief interface of Table
 * @file CacheStorageFactory.h
 * @author: jimmyshi
 * @date: 2022-10-20
 */
#pragma once

#include "bcos-framework/storage/StorageInterface.h"

namespace bcos::storage
{
class CacheStorageFactory
{
public:
    using Ptr = std::shared_ptr<CacheStorageFactory>;
    CacheStorageFactory(
        bcos::storage::TransactionalStorageInterface::Ptr backendStorage, ssize_t cacheSize)
      : m_cacheSize(cacheSize), m_backendStorage(backendStorage)
    {}

    virtual ~CacheStorageFactory() = default;
    storage::MergeableStorageInterface::Ptr build();

private:
    ssize_t m_cacheSize;
    bcos::storage::TransactionalStorageInterface::Ptr m_backendStorage;
};

}  // namespace bcos::storage