/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file testSchedulerFactory.cpp
 * @brief A scheduler built by SchedulerFactory executes blocks on the pool the factory was given.
 */
#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-protocol/bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/SchedulerFactory.h"
#include "bcos-utilities/IOServicePool.h"
#include "mock/MockBlockExecutiveFactory.h"
#include "mock/MockLedger3.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/test/unit_test.hpp>
#include <future>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(testSchedulerFactory)

// SchedulerImpl::executeBlock posts onto a strand of its IOServicePool; the executor-version-0
// lane builds every SchedulerImpl through SchedulerFactory, so the factory has to hand the pool
// on or the first executeBlock dereferences a null strand.
BOOST_AUTO_TEST_CASE(builtSchedulerExecutesBlockOnTheGivenPool)
{
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "test-sched-factory");
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto suite = std::make_shared<bcos::crypto::CryptoSuite>(
        hashImpl, std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(suite,
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite));

    rocksdb::DB* db = nullptr;
    rocksdb::Options options;
    options.create_if_missing = true;
    BOOST_REQUIRE(rocksdb::DB::Open(options, "./unittestdb-scheduler-factory", &db).ok());
    auto storage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);

    bcos::scheduler::SchedulerFactory factory(std::make_shared<bcos::scheduler::ExecutorManager>(),
        std::make_shared<MockLedger3>(), storage,
        std::make_shared<bcos::executor::NativeExecutionMessageFactory>(), blockFactory,
        std::make_shared<MockTxPool>(),
        std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>(), hashImpl, false,
        false, 0, ioServicePool);
    auto scheduler = factory.build(0);
    scheduler->setBlockExecutiveFactory(std::make_shared<MockBlockExecutiveFactory>(false));

    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(1);
    std::promise<void> called;
    scheduler->executeBlock(block, false,
        [&](bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr, bool) { called.set_value(); });
    BOOST_CHECK(
        called.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready);

    scheduler.reset();
    ioServicePool.reset();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
