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
 * @file DeferredWriteBatch.h
 * @author: kyonRay
 * @date: 2026-06-23
 */
#pragma once
#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
#include <string_view>

namespace bcos::storage2::rocksdb
{
/// Single-CF (default) atomic write handle. Move-only; one commit then inert.
/// Accumulates Put/Delete on the default column family and flushes them with a
/// single db->Write(). Used by mergeBackStorageWithMPT (PR-14b) to atomically
/// persist /apps/* flat state + /mpt/<hash> nodes + manifest deletes.
class DeferredWriteBatch
{
public:
    explicit DeferredWriteBatch(::rocksdb::DB& db);
    ~DeferredWriteBatch() = default;
    DeferredWriteBatch(DeferredWriteBatch const&) = delete;
    DeferredWriteBatch& operator=(DeferredWriteBatch const&) = delete;
    DeferredWriteBatch(DeferredWriteBatch&&) noexcept = default;
    DeferredWriteBatch& operator=(DeferredWriteBatch&&) noexcept = default;

    void put(std::string_view key, std::string_view value);
    void del(std::string_view key);
    ::rocksdb::Status commit() noexcept;  // one-shot; second call -> InvalidArgument
    ::rocksdb::WriteBatch& rawBatch() noexcept { return m_batch; }

private:
    ::rocksdb::DB* m_db;
    ::rocksdb::WriteBatch m_batch;
    bool m_committed = false;
};
}  // namespace bcos::storage2::rocksdb
