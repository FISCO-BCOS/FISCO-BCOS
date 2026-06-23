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
 * @brief Single-CF atomic write handle over rocksdb::WriteBatch (spec §5.6 / §5.7)
 * @file DeferredWriteBatch.cpp
 * @author: kyonRay
 * @date: 2026-06-23
 */
#include "DeferredWriteBatch.h"
#include <rocksdb/slice.h>

namespace bcos::storage2::rocksdb
{
DeferredWriteBatch::DeferredWriteBatch(::rocksdb::DB& db) : m_db(std::addressof(db)) {}

void DeferredWriteBatch::put(std::string_view key, std::string_view value)
{
    m_batch.Put(m_db->DefaultColumnFamily(), ::rocksdb::Slice{key.data(), key.size()},
        ::rocksdb::Slice{value.data(), value.size()});
}

void DeferredWriteBatch::del(std::string_view key)
{
    m_batch.Delete(m_db->DefaultColumnFamily(), ::rocksdb::Slice{key.data(), key.size()});
}

::rocksdb::Status DeferredWriteBatch::commit() noexcept
{
    if (m_committed)
    {
        return ::rocksdb::Status::InvalidArgument("DeferredWriteBatch already committed");
    }
    m_committed = true;
    // WriteOptions.sync left at default — spec §5.6 forbids relaxing durability for MPT nodes.
    ::rocksdb::WriteOptions wopts;
    return m_db->Write(wopts, &m_batch);
}
}  // namespace bcos::storage2::rocksdb
