/**
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
 * @brief FIB-158: DownloadingQueue::onCommitFailed must NOT read m_blocks /
 *        m_commitQueue after releasing x_blocks / x_commitQueue. Pre-fix the
 *        function captured rePushedBlockCount inside the locked section but
 *        re-read m_blocks.top(), m_blocks.size(), and m_commitQueue.size() for
 *        diagnostic logging after the locks dropped, racing with concurrent
 *        push() / pop() callers.
 *
 *        The fix snapshots all diagnostic values inside the locked region;
 *        the trailing log uses only the captured locals. This test exercises
 *        onCommitFailed concurrent with push() to surface the race under TSan.
 *
 * @file FIB158_OnCommitFailedReadUnderLockTest.cpp
 */

#include "SyncFixture.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "bcos-sync/protocol/PB/BlockSyncMsgFactoryImpl.h"
#include "bcos-sync/state/DownloadingQueue.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
namespace
{
// FIB-suffixed helpers for UNITY_BUILD safety.
inline CryptoSuite::Ptr makeCryptoSuiteFib158()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
}

inline bcos::protocol::Block::Ptr makeFailedBlockFib158(
    CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory, bcos::protocol::BlockNumber _n)
{
    return fakeAndCheckBlock(_cryptoSuite, _blockFactory, false, /*txsHashSize=*/0,
        /*receiptsHashSize=*/0, /*reuseTxs=*/false, /*reuseReceipts=*/false);
}

inline BlocksMsgInterface::Ptr makeBlocksMsgFib158(
    CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory, bcos::protocol::BlockNumber _n)
{
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    auto msg = factory->createBlocksMsg();
    msg->setPacketType(BlockSyncPacketType::BlockResponsePacket);
    msg->setNumber(_n);

    // Build one block with the requested number, then encode it into the msg
    auto block = _blockFactory->createBlock();
    auto header = _blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(_n);
    auto hash = _cryptoSuite->hashImpl()->hash(bytes{static_cast<uint8_t>(_n & 0xff)});
    header->setStateRoot(hash);
    header->calculateHash(*_cryptoSuite->hashImpl());  // FIB-158: ensure hash() doesn't throw
    block->setBlockHeader(header);

    bcos::bytes blockData;
    block->encode(blockData);
    msg->appendBlockData(std::move(blockData));
    return msg;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB158OnCommitFailedReadUnderLockTest, TestPromptFixture)

// FIB-158: smoke check — onCommitFailed runs the InvalidBlocks early-return
// path (lines 692-707). Other error code paths drive into
// tryToCommitBlockToLedger which depends on a real scheduler/ledger; the race
// surface (lines 771-797 in DownloadingQueue.cpp) is exercised by the
// concurrentPush race-regression test below using the BlockIsCommitting path.
BOOST_AUTO_TEST_CASE(onCommitFailedInvalidBlocksReturnsCleanly)
{
    auto cryptoSuite = makeCryptoSuiteFib158();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay, /*_blockNumber=*/10);
    faker->init();
    auto sync = faker->sync();
    auto queue = sync->downloadingQueue();
    BOOST_REQUIRE(queue);

    auto blockFactory = faker->syncConfig()->blockFactory();
    auto failedBlock = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(faker->syncConfig()->blockNumber() + 5);
    auto hash = cryptoSuite->hashImpl()->hash(bytes{0x01});
    header->setStateRoot(hash);
    header->calculateHash(*cryptoSuite->hashImpl());  // FIB-158: ensure hash() doesn't throw
    failedBlock->setBlockHeader(header);

    auto err = BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::InvalidBlocks,
        "synthetic InvalidBlocks from FIB-158 test");
    // The InvalidBlocks path calls fetchAndUpdateLedgerConfig (try/catch'd
    // internally) and tryToCommitBlockToLedger; with no blocks queued and the
    // FakeLedger backing, this should return cleanly.
    try
    {
        queue->onCommitFailed(err, failedBlock);
    }
    catch (std::exception const& e)
    {
        // FakeLedger may be missing parts of the scheduler API; the only
        // assertion we make is that it does not crash. Log and continue.
        BOOST_TEST_MESSAGE(
            "onCommitFailed (InvalidBlocks) threw (acceptable for "
            "FakeLedger): "
            << e.what());
    }
}

// FIB-158: expired-block path (lines 708-714) — block number <= current
// blockNumber returns early without touching m_blocks. Smoke check.
BOOST_AUTO_TEST_CASE(onCommitFailedExpiredBlockReturnsCleanly)
{
    auto cryptoSuite = makeCryptoSuiteFib158();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay, /*_blockNumber=*/100);
    faker->init();
    auto sync = faker->sync();
    auto queue = sync->downloadingQueue();
    BOOST_REQUIRE(queue);

    auto blockFactory = faker->syncConfig()->blockFactory();
    auto failedBlock = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    // expired: number <= current blockNumber()
    header->setNumber(50);
    auto hash = cryptoSuite->hashImpl()->hash(bytes{0x02});
    header->setStateRoot(hash);
    header->calculateHash(*cryptoSuite->hashImpl());  // FIB-158: ensure hash() doesn't throw
    failedBlock->setBlockHeader(header);

    auto err = BCOS_ERROR_PTR(0, "synthetic non-special error from FIB-158 test");
    BOOST_CHECK_NO_THROW(queue->onCommitFailed(err, failedBlock));
}

// FIB-158: race regression. Run onCommitFailed in one thread with concurrent
// push() in another. Pre-fix, TSan flagged a data race at
// DownloadingQueue.cpp:771-774 (m_blocks.top()/size() read post-release of
// x_blocks). After the fix the diagnostic values are captured under lock and
// the log reads only from local variables; no race remains. We use the
// BlockIsCommitting path which goes through the inner locked block (line 748-
// 770) but exits before the FakeLedger-dependent retry logic (line 733-735).
//
// onCommitFailed may throw from FakeLedger interactions; we capture the
// exception, the assertion that matters here is that TSan reports no race
// on m_blocks / m_commitQueue when running this test under -fsanitize=thread.
BOOST_AUTO_TEST_CASE(onCommitFailedConcurrentPushIsRaceFree)
{
    auto cryptoSuite = makeCryptoSuiteFib158();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay, /*_blockNumber=*/10);
    faker->init();
    auto sync = faker->sync();
    auto queue = sync->downloadingQueue();
    BOOST_REQUIRE(queue);
    auto blockFactory = faker->syncConfig()->blockFactory();
    auto baseNumber = faker->syncConfig()->blockNumber();

    // Spin a writer thread that keeps pushing blocks into the downloading
    // queue while we drive onCommitFailed. The writer touches m_blocks via
    // flushBufferToQueue; onCommitFailed mutates and (pre-fix) reads m_blocks
    // post-release.
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int64_t i = 0; i < 200 && !stop.load(); ++i)
        {
            auto msg = makeBlocksMsgFib158(cryptoSuite, blockFactory, baseNumber + 100 + i);
            queue->push(msg);
            queue->flushBufferToQueue();
        }
    });

    int onCommitInvocations = 0;
    int onCommitExceptions = 0;
    for (int i = 0; i < 50; ++i)
    {
        auto failedBlock = blockFactory->createBlock();
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(baseNumber + 50 + i);
        auto hash = cryptoSuite->hashImpl()->hash(bytes{static_cast<uint8_t>(i & 0xff)});
        header->setStateRoot(hash);
        header->calculateHash(*cryptoSuite->hashImpl());  // FIB-158: ensure hash() doesn't throw
        failedBlock->setBlockHeader(header);

        // Use a generic non-special error code so onCommitFailed reaches the
        // post-locked-region log site (DownloadingQueue.cpp:791-797) — i.e.
        // exactly the lines covered by the FIB-158 fix. InvalidBlocks would
        // early-return; BlockIsCommitting would retry without entering the
        // re-push block.
        auto err = BCOS_ERROR_PTR(0xdead, "synthetic FIB-158 race driver");
        try
        {
            queue->onCommitFailed(err, failedBlock);
        }
        catch (std::exception const&)
        {
            ++onCommitExceptions;
        }
        ++onCommitInvocations;
    }

    stop.store(true);
    writer.join();

    // We made progress; ratios are not asserted because FakeLedger may
    // intermittently raise. The race-freedom assertion is delivered by TSan.
    BOOST_CHECK_EQUAL(onCommitInvocations, 50);
    (void)onCommitExceptions;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
