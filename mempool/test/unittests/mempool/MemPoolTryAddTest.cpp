/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
/// Distinct names from MemPoolImplTest.cpp on purpose: both files land in one translation unit
/// under UNITY_BUILD, where a second `TestTransactionImpl` would be a redefinition rather than a
/// second local helper.
class TryAddTransaction : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

/// @param salt goes into the input field, which the tars dataHash covers -- two transactions
/// with the same (sender, nonce) and different salts are the pair the "already taken" rule is
/// about.
std::shared_ptr<TryAddTransaction> makeTryAddTx(
    std::string_view senderBytes, std::string_view nonce, std::string_view salt = {})
{
    auto tx = std::make_shared<TryAddTransaction>();
    tx->mutableInner().data.to.assign(senderBytes.begin(), senderBytes.end());
    tx->mutableInner().data.input.assign(salt.begin(), salt.end());
    tx->setNonce(std::string(nonce));
    tx->forceSender(bcos::bytes(reinterpret_cast<const byte*>(senderBytes.data()),
        reinterpret_cast<const byte*>(senderBytes.data()) + senderBytes.size()));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    return tx;
}

const std::string c_sender("aaaaaaaaaaaaaaaaaaaa", 20);
}  // namespace

BOOST_AUTO_TEST_SUITE(MemPoolTryAddTest)

// The eight endings of add(), which reports none of them, are one status each here. Four are
// silent returns in add(): two of them (no computable hash, unreadable nonce) leave a caller
// holding a hash for a transaction the pool never stored, one (a taken (sender, nonce)) quietly
// replaces an earlier submitter's transaction, and one (a duplicate hash) is harmless but
// indistinguishable from a fresh admission.

BOOST_AUTO_TEST_CASE(admittedTransactionReportsNone)
{
    MemPoolImpl pool;
    BOOST_CHECK(pool.tryAdd(makeTryAddTx(c_sender, "0")) == TransactionStatus::None);
    // ... and it is in the pool, which the next attempt at the same hash reports.
    BOOST_CHECK(pool.tryAdd(makeTryAddTx(c_sender, "0")) == TransactionStatus::AlreadyInTxPool);
}

BOOST_AUTO_TEST_CASE(hexAndDecimalNoncesAreTheSameNonce)
{
    // Web3 transactions carry "0x"-prefixed nonces and BCOS ones decimal; TransactionData parses
    // both to the same int64, so "0x10" and "16" are one slot, not two.
    MemPoolImpl pool;
    BOOST_CHECK(pool.tryAdd(makeTryAddTx(c_sender, "0x10")) == TransactionStatus::None);
    BOOST_CHECK(
        pool.tryAdd(makeTryAddTx(c_sender, "16", "salt")) == TransactionStatus::NonceCheckFail);
}

// First come first served, the same rule the other pool applies through insertMemoryNonce. This
// is where tryAdd and add deliberately disagree: add() REPLACES the stored transaction.
BOOST_AUTO_TEST_CASE(takenSenderNoncePairIsRefusedAndTheStoredTransactionStays)
{
    MemPoolImpl pool;
    auto first = makeTryAddTx(c_sender, "7");
    auto second = makeTryAddTx(c_sender, "7", "different-body");
    BOOST_REQUIRE(first->hash() != second->hash());

    BOOST_CHECK(pool.tryAdd(first) == TransactionStatus::None);
    BOOST_CHECK(pool.tryAdd(second) == TransactionStatus::NonceCheckFail);
    // The first is still the one the pool holds: it answers "already in", and the second still
    // answers "nonce taken" rather than "already in".
    BOOST_CHECK(pool.tryAdd(first) == TransactionStatus::AlreadyInTxPool);
    BOOST_CHECK(pool.tryAdd(second) == TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(addStillReplacesWhereTryAddRefuses)
{
    // The divergence above, stated from add()'s side so that changing either one is a visible
    // decision. add() has no production caller left once the RPC entry moves to tryAdd -- the
    // sealing tests here and in engine/ are what still exercise it -- but it is the MemPool
    // concept's admission method (bcos-framework/mempool/MemPool.h), so it stays.
    MemPoolImpl pool;
    auto first = makeTryAddTx(c_sender, "7");
    auto second = makeTryAddTx(c_sender, "7", "different-body");
    pool.add(std::vector<protocol::Transaction::Ptr>{first});
    pool.add(std::vector<protocol::Transaction::Ptr>{second});
    // The replacement took the slot, so the first one's hash is gone and re-offering it is not
    // "already in the pool" -- it is the taken pair.
    BOOST_CHECK(pool.tryAdd(second) == TransactionStatus::AlreadyInTxPool);
    BOOST_CHECK(pool.tryAdd(first) == TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(unreadableNonceIsReported)
{
    MemPoolImpl pool;
    BOOST_CHECK(pool.tryAdd(makeTryAddTx(c_sender, "nonce")) == TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(uncomputableHashIsReported)
{
    class UnhashableTransaction : public TryAddTransaction
    {
    public:
        [[nodiscard]] bcos::crypto::HashType hash() const override
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("hash unavailable"));
        }
    };
    auto tx = std::make_shared<UnhashableTransaction>();
    tx->setNonce("0");
    tx->markClean();

    MemPoolImpl pool;
    BOOST_CHECK(pool.tryAdd(std::move(tx)) == TransactionStatus::Malformed);
}

// The three that are not verdicts about the transaction: reporting them as a status would let a
// caller that skipped admission carry on as if the pool had judged the transaction.

BOOST_AUTO_TEST_CASE(nullTransactionThrows)
{
    MemPoolImpl pool;
    BOOST_CHECK_THROW(pool.tryAdd(nullptr), NullTransaction);
}

BOOST_AUTO_TEST_CASE(taintedTransactionThrows)
{
    MemPoolImpl pool;
    auto tx = std::make_shared<TryAddTransaction>();
    tx->setNonce("0");
    BOOST_REQUIRE(tx->tainted());  // the default: nothing has verified this signature
    BOOST_CHECK_THROW(pool.tryAdd(std::move(tx)), InvalidTaintedTransaction);
}

BOOST_AUTO_TEST_CASE(blobEnvelopeThrows)
{
    MemPoolImpl pool;
    auto tx = std::make_shared<TryAddTransaction>();
    tx->mutableInner().type = static_cast<int>(TransactionType::Web3Transaction);
    bcos::bytes const blobPayload{0x03, 0x01, 0x02};
    tx->mutableInner().extraTransactionBytes.assign(blobPayload.begin(), blobPayload.end());
    tx->setNonce("0");
    tx->markClean();
    BOOST_CHECK_THROW(pool.tryAdd(std::move(tx)), InvalidBlobTransaction);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
