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
 * @file P2PMirrorForgeryTest.cpp
 * @brief A peer can rewrite a Web3 transaction's tars mirror without touching its signature or
 *        its canonical hash. TransactionSync::verifyFetchedTxs must repair the mirror from the
 *        signed envelope before anything downstream reads it.
 */

#include "bcos-tx-validator/Normalize.h"
#include "bcos-txpool/sync/utilities/Common.h"
#include "test/unittests/txpool/TxPoolFixture.h"

#include <bcos-codec/rlp/Common.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::txpool;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
/// A real signed EIP-1559 transaction (the vector NormalizeTest round-trips).
constexpr std::string_view kRaw1559 =
    "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc"
    "16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb"
    "697baef842a00000000000000000000000000000000000000000000000000000000000000003a0000000000000"
    "0000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c"
    "189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741"
    "c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";

constexpr std::string_view kAttackerAddress = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

/// Project the raw transaction to tars the way the RPC ingress does, then fill
/// extraTransactionHash with the canonical hash. This is an honest transaction.
std::shared_ptr<bcostars::protocol::TransactionImpl> makeSignedTx()
{
    auto raw = fromHexWithPrefix(kRaw1559);
    auto ref = bcos::ref(raw);
    rpc::Web3Transaction web3;
    auto err = codec::rlp::decode(ref, web3);
    BOOST_REQUIRE_MESSAGE(err == nullptr, "fixture must decode");

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = web3.takeToTarsTransaction()]() mutable { return &inner; });
    crypto::Keccak256 hasher;
    tx->calculateHash(hasher);
    BOOST_REQUIRE(!tx->inner().extraTransactionHash.empty());
    return tx;
}

/// TransactionSync that stops at the import boundary and keeps what it was handed, so a test can
/// assert on the transactions exactly as the import path would see them.
class RecordingSync : public TransactionSync
{
public:
    using TransactionSync::TransactionSync;
    using TransactionSync::verifyFetchedTxs;

    std::tuple<bool, std::shared_ptr<Transactions>> importDownloadedTxsByBlock(
        Block::Ptr const& _txsBuffer, Block::ConstPtr /*unused*/ = nullptr) override
    {
        auto txs = std::make_shared<Transactions>();
        auto txFactory = m_config->blockFactory()->transactionFactory();
        for (auto&& tx : _txsBuffer->transactions())
        {
            txs->emplace_back(txFactory->createTransaction(*tx));
        }
        imported = txs;
        return {true, txs};
    }

    std::shared_ptr<Transactions> imported;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(P2PMirrorForgeryTest, TxPoolFixture)

/// The mechanism the fix rests on. Block::transactions() hands out AnyTransaction values, each
/// holding a TransactionImpl that points into the block's own m_inner.transactions -- so a write
/// through the view reaches the block, not a copy of it. If that were false the normalize() loop
/// in verifyFetchedTxs would repair a temporary and throw the result away, and every other test
/// here would still pass for the wrong reason.
BOOST_AUTO_TEST_CASE(writingThroughTheBlockViewReachesTheBlock)
{
    auto tx = makeSignedTx();
    const auto honestTo = tx->inner().data.to;
    BOOST_REQUIRE(!honestTo.empty());
    BOOST_REQUIRE(honestTo != kAttackerAddress);
    tx->mutableInner().data.to = kAttackerAddress;

    auto block = blockFactory()->createBlock();
    block->appendTransaction(tx);
    BOOST_REQUIRE_EQUAL(block->transactionsSize(), 1U);

    // Rewrite through one view...
    for (auto&& blockTx : block->transactions())
    {
        BOOST_CHECK(txvalidator::normalize(*blockTx) == TransactionStatus::None);
    }

    // ...and read back through a freshly created one.
    for (auto&& blockTx : block->transactions())
    {
        BOOST_CHECK_EQUAL(std::string(blockTx->to()), honestTo);
    }
}

/// The attack. The signature covers only the envelope and the canonical hash is derived from it,
/// so rewriting data.to leaves both intact: the transaction still recovers to the victim and
/// still answers the hash the requester asked for. Only the mirror -- which is what execution
/// reads -- points at the attacker.
BOOST_AUTO_TEST_CASE(forgedRecipientIsRepairedBeforeImportSeesIt)
{
    auto tx = makeSignedTx();
    const auto honestTo = tx->inner().data.to;
    const auto canonicalHash = tx->hash();
    tx->mutableInner().data.to = kAttackerAddress;
    // The forgery does not disturb the hash the requester will match against.
    BOOST_REQUIRE_EQUAL(tx->hash().hex(), canonicalHash.hex());

    auto block = blockFactory()->createBlock();
    block->appendTransaction(tx);
    bytes encodedBlock;
    block->encode(encodedBlock);

    auto syncConfig = sync()->config();
    auto msg = syncConfig->msgFactory()->createTxsSyncMsg();
    msg->setType(static_cast<int32_t>(TxsSyncPacketType::TxsResponsePacket));
    msg->setTxsData(encodedBlock);
    auto packet = msg->encode();

    auto recording = std::make_shared<RecordingSync>(syncConfig, false);
    auto missedTxs = std::make_shared<crypto::HashList>();
    missedTxs->emplace_back(canonicalHash);

    bool finished = false;
    Error::Ptr error;
    recording->verifyFetchedTxs(nullptr, nodeID(), bcos::ref(*packet), missedTxs, nullptr,
        [&](Error::Ptr _error, bool /*unused*/) {
            error = std::move(_error);
            finished = true;
        });

    BOOST_REQUIRE(finished);
    BOOST_REQUIRE_MESSAGE(!error, "the hash gate must still accept: the forgery does not move it");
    BOOST_REQUIRE(recording->imported);
    BOOST_REQUIRE_EQUAL(recording->imported->size(), 1U);
    // Repaired, not rejected: normalize rewrites the mirror to the signed values and returns None.
    BOOST_CHECK_EQUAL(std::string(recording->imported->at(0)->to()), honestTo);
}

/// A forged hash is the one case normalize() rejects rather than repairs. The requested hash here
/// is the FORGED one, which is what a peer that fabricated it would claim we asked for: the
/// expectedHash gate then compares the peer's invention against itself and lets it through. Only
/// recomputing the hash from the signed envelope catches it, so this case fails without the fix.
BOOST_AUTO_TEST_CASE(forgedWireHashIsRejectedBeforeImport)
{
    auto tx = makeSignedTx();
    auto& wireHash = tx->mutableInner().extraTransactionHash;
    wireHash.assign(bcos::crypto::HashType::SIZE, static_cast<tars::Char>(0x11));
    const auto forgedHash = tx->hash();

    auto block = blockFactory()->createBlock();
    block->appendTransaction(tx);
    bytes encodedBlock;
    block->encode(encodedBlock);

    auto syncConfig = sync()->config();
    auto msg = syncConfig->msgFactory()->createTxsSyncMsg();
    msg->setType(static_cast<int32_t>(TxsSyncPacketType::TxsResponsePacket));
    msg->setTxsData(encodedBlock);
    auto packet = msg->encode();

    auto recording = std::make_shared<RecordingSync>(syncConfig, false);
    auto missedTxs = std::make_shared<crypto::HashList>();
    missedTxs->emplace_back(forgedHash);

    bool finished = false;
    Error::Ptr error;
    recording->verifyFetchedTxs(nullptr, nodeID(), bcos::ref(*packet), missedTxs, nullptr,
        [&](Error::Ptr _error, bool /*unused*/) {
            error = std::move(_error);
            finished = true;
        });

    BOOST_REQUIRE(finished);
    BOOST_CHECK(error);
    BOOST_CHECK(!recording->imported);
}

/// A BCOS transaction's dataHash already covers the whole TransactionData, so normalize() returns
/// None without touching it. The loop must not turn the sync path into a Web3-only path.
BOOST_AUTO_TEST_CASE(bcosTransactionsPassThroughTheLoopUntouched)
{
    auto tx = fakeTransaction(m_cryptoSuite);
    const auto canonicalHash = tx->hash();

    auto block = blockFactory()->createBlock();
    block->appendTransaction(tx);
    bytes encodedBlock;
    block->encode(encodedBlock);

    auto syncConfig = sync()->config();
    auto msg = syncConfig->msgFactory()->createTxsSyncMsg();
    msg->setType(static_cast<int32_t>(TxsSyncPacketType::TxsResponsePacket));
    msg->setTxsData(encodedBlock);
    auto packet = msg->encode();

    auto recording = std::make_shared<RecordingSync>(syncConfig, false);
    auto missedTxs = std::make_shared<crypto::HashList>();
    missedTxs->emplace_back(canonicalHash);

    bool finished = false;
    Error::Ptr error;
    recording->verifyFetchedTxs(nullptr, nodeID(), bcos::ref(*packet), missedTxs, nullptr,
        [&](Error::Ptr _error, bool /*unused*/) {
            error = std::move(_error);
            finished = true;
        });

    BOOST_REQUIRE(finished);
    BOOST_CHECK(!error);
    BOOST_REQUIRE(recording->imported);
    BOOST_CHECK_EQUAL(recording->imported->at(0)->hash().hex(), canonicalHash.hex());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
