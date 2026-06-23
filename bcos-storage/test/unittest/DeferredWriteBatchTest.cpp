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
 * @brief Unit tests for DeferredWriteBatch (single-CF atomic write helper)
 * @file DeferredWriteBatchTest.cpp
 * @author: kyonRay
 * @date: 2026-06-23
 */
#include <bcos-storage/DeferredWriteBatch.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-utilities/FixedBytes.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <random>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::rocksdb;

BOOST_AUTO_TEST_SUITE(DeferredWriteBatchSuite)

namespace
{
std::string dwbUniqueTempPath()
{
    return "./dwb_test_" + std::to_string(std::random_device{}());
}

::rocksdb::DB* dwbOpenDB(const std::string& path)
{
    ::rocksdb::DB* rawPtr = nullptr;
    ::rocksdb::Options options;
    options.create_if_missing = true;
    ::rocksdb::Status s = ::rocksdb::DB::Open(options, path, &rawPtr);
    BOOST_REQUIRE(s.ok());
    BOOST_REQUIRE(rawPtr != nullptr);
    return rawPtr;
}

std::string dwbGet(::rocksdb::DB& db, std::string_view key, bool& found)
{
    std::string value;
    auto status = db.Get(::rocksdb::ReadOptions(), db.DefaultColumnFamily(),
        ::rocksdb::Slice{key.data(), key.size()}, &value);
    found = status.ok();
    if (!found)
    {
        BOOST_REQUIRE(status.IsNotFound());
    }
    return value;
}
}  // namespace

BOOST_AUTO_TEST_CASE(MultiPutCommitsAtomically)
{
    std::string path = dwbUniqueTempPath();
    ::rocksdb::DB* rawPtr = dwbOpenDB(path);

    // Inner scope: keep dbOwner/batch lifetime strictly shorter than the on-disk path so the
    // DB is fully closed (background compaction/flush threads release lock/SST files) before
    // remove_all runs — otherwise remove_all races the close path on lock-tracking filesystems.
    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);

        // Seed a key we will delete via the batch.
        ::rocksdb::WriteOptions wo;
        BOOST_REQUIRE(dbOwner
                ->Put(wo, dbOwner->DefaultColumnFamily(), std::string{"/apps/to_delete"},
                    std::string{"old_value"})
                .ok());

        h256 hash = h256::generateRandomFixedBytes();
        std::string mptKey = makeMPTNodeKey(hash);

        DeferredWriteBatch batch(*dbOwner);
        batch.put("/apps/acc1:balance", "100");
        batch.put("/apps/acc2:balance", "200");
        batch.put(mptKey, "mpt_node_data");
        batch.del("/apps/to_delete");

        BOOST_CHECK(batch.commit().ok());

        bool found = false;
        BOOST_CHECK_EQUAL(dwbGet(*dbOwner, "/apps/acc1:balance", found), "100");
        BOOST_CHECK(found);
        BOOST_CHECK_EQUAL(dwbGet(*dbOwner, "/apps/acc2:balance", found), "200");
        BOOST_CHECK(found);
        BOOST_CHECK_EQUAL(dwbGet(*dbOwner, mptKey, found), "mpt_node_data");
        BOOST_CHECK(found);

        // Deleted key must be gone.
        dwbGet(*dbOwner, "/apps/to_delete", found);
        BOOST_CHECK(!found);
    }

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(EmptyBatchCommitIsNoop)
{
    std::string path = dwbUniqueTempPath();
    ::rocksdb::DB* rawPtr = dwbOpenDB(path);

    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);
        DeferredWriteBatch batch(*dbOwner);
        BOOST_CHECK(batch.commit().ok());
    }

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(DoubleCommitReturnsInvalidArgument)
{
    std::string path = dwbUniqueTempPath();
    ::rocksdb::DB* rawPtr = dwbOpenDB(path);

    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);
        DeferredWriteBatch batch(*dbOwner);
        batch.put("/apps/k", "v");
        BOOST_CHECK(batch.commit().ok());
        BOOST_CHECK(batch.commit().IsInvalidArgument());
    }

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(MoveSemanticsTransferOwnership)
{
    std::string path = dwbUniqueTempPath();
    ::rocksdb::DB* rawPtr = dwbOpenDB(path);

    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);

        DeferredWriteBatch batch1(*dbOwner);
        batch1.put("/apps/moved", "after_move");

        DeferredWriteBatch batch2 = std::move(batch1);
        BOOST_CHECK(batch2.commit().ok());

        bool found = false;
        BOOST_CHECK_EQUAL(dwbGet(*dbOwner, "/apps/moved", found), "after_move");
        BOOST_CHECK(found);
    }

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(FactoryFromStorageCommits)
{
    std::string path = dwbUniqueTempPath();
    ::rocksdb::DB* rawPtr = dwbOpenDB(path);

    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);

        RocksDBStorage2<executor_v1::StateKey, storage::Entry, StateKeyResolver, StateValueResolver>
            storage(*dbOwner, StateKeyResolver{}, StateValueResolver{});

        auto batch = storage.makeDeferredBatch();
        batch.put("/apps/factory_key", "factory_value");
        BOOST_CHECK(batch.commit().ok());

        bool found = false;
        BOOST_CHECK_EQUAL(dwbGet(*dbOwner, "/apps/factory_key", found), "factory_value");
        BOOST_CHECK(found);
    }

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_SUITE_END()
