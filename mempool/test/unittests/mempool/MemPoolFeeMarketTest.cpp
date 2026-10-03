/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/kzg/Kzg4844.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/mempool/MemPool.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <iterator>
#include <range/v3/all.hpp>
#include <unordered_map>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::executor_v1;

namespace bcos::test
{
// The same in-memory state shape MemPoolImplTest uses; duplicated here so this file is
// self-contained (seal needs to read account nonces).
struct FeeMarketStateStorage
{
    using Value = storage::Entry;
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> data;
    task::Task<std::optional<Value>> readOne(StateKeyView key)
    {
        auto [table, field] = key.get();
        if (auto tIt = data.find(std::string(table)); tIt != data.end())
        {
            if (auto fIt = tIt->second.find(std::string(field)); fIt != tIt->second.end())
            {
                co_return std::make_optional(fIt->second);
            }
        }
        co_return std::nullopt;
    }

    task::Task<std::optional<Value>> readOne(StateKey key)
    {
        co_return co_await readOne(StateKeyView{key});
    }

    template <class Keys>
    task::Task<std::vector<std::optional<Value>>> readSome(Keys keys)
    {
        std::vector<std::optional<Value>> results;
        for (auto&& key : keys)
        {
            results.emplace_back(co_await readOne(StateKeyView{key}));
        }
        co_return results;
    }

    task::Task<void> writeOne(StateKey key, Value value)
    {
        StateKeyView view{key};
        auto [table, field] = view.get();
        data[std::string(table)][std::string(field)] = std::move(value);
        co_return;
    }

    task::Task<void> writeOne(StateKeyView key, Value value)
    {
        auto [table, field] = key.get();
        data[std::string(table)][std::string(field)] = std::move(value);
        co_return;
    }

    template <class KVs>
    task::Task<void> writeSome(KVs keyValues)
    {
        for (auto&& kv : keyValues)
        {
            StateKey key{std::get<0>(kv)};
            auto& value = std::get<1>(kv);
            StateKeyView view{key};
            auto [table, field] = view.get();
            data[std::string(table)][std::string(field)] = value;
        }
        co_return;
    }

    task::Task<bool> existsOne(StateKeyView key)
    {
        auto value = co_await readOne(key);
        co_return value.has_value();
    }

    task::Task<bool> existsOne(StateKey key) { co_return co_await existsOne(StateKeyView{key}); }
};

class FeeMarketTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

static bytes rawBytes(std::string_view s)
{
    return {reinterpret_cast<const byte*>(s.data()),
        reinterpret_cast<const byte*>(s.data()) + s.size()};
}

static std::string toHexU256(u256 value)
{
    return value == 0 ? std::string{"0x0"} : std::string{"0x"} + value.str(0, std::ios_base::hex);
}

/// A transaction carrying EIP-1559 fee fields. Deliberately NOT a Web3-typed transaction:
/// the fee-market ordering reads the fee fields off the tars data, and a Web3 type would
/// force calculateHash() through reassembleWeb3RawTransaction (needs a full envelope).
static protocol::Transaction::Ptr makeFeeTx(
    std::string_view senderBytes, int64_t nonce, u256 tipCap, u256 feeCap)
{
    auto tx = std::make_shared<FeeMarketTransactionImpl>();
    tx->mutableInner().data.to.assign(senderBytes.begin(), senderBytes.end());
    tx->mutableInner().data.maxPriorityFeePerGas = toHexU256(tipCap);
    tx->mutableInner().data.maxFeePerGas = toHexU256(feeCap);
    // The tars data hash does not cover the fee strings, so two same-(sender, nonce) txs
    // with different fees would hash identically and trip the hash dedup instead of the
    // (sender, nonce) replacement path under test. Feed the fees into the input bytes.
    auto const tip = toHexU256(tipCap);
    auto const cap = toHexU256(feeCap);
    tx->mutableInner().data.input.assign(tip.begin(), tip.end());
    tx->mutableInner().data.input.insert(
        tx->mutableInner().data.input.end(), cap.begin(), cap.end());
    tx->setNonce(std::to_string(nonce));
    tx->forceSender(rawBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    return tx;
}

/// The EIP-4844 signing preimage 0x03 || rlp([chainId, nonce, tip, feeCap, gas, to, value,
/// data, accessList, maxFeePerBlobGas, blobVersionedHashes]) — a well-formed envelope so
/// calculateHash()'s reassemble (decode + splice + keccak, no ecrecover) accepts it.
static bcos::bytes makeBlobSigningPayload(std::size_t blobCount)
{
    bcos::bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));      // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));      // nonce
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));      // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1000000000));  // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));  // gasLimit
    bcos::codec::rlp::encode(body, bcos::Address("1111111111111111111111111111111111111111"));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));      // value
    bcos::codec::rlp::encode(body, bcos::bytes{});                 // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);              // empty accessList
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1000000000));  // maxFeePerBlobGas
    bcos::bytes hashesBody;
    for (std::size_t i = 0; i < blobCount; ++i)
    {
        bcos::bytes hash(32, static_cast<bcos::byte>(0x30 + i));
        bcos::codec::rlp::encode(hashesBody, hash);
    }
    bcos::codec::rlp::encodeHeader(
        body, bcos::codec::rlp::Header{.isList = true, .payloadLength = hashesBody.size()});
    body.insert(body.end(), hashesBody.begin(), hashesBody.end());
    bcos::bytes payload;
    payload.push_back(0x03);
    bcos::codec::rlp::encodeHeader(
        payload, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());
    return payload;
}

/// A type-3 (blob) Web3 transaction with @p blobCount versioned hashes.
static protocol::Transaction::Ptr makeBlobTx(
    std::string_view senderBytes, int64_t nonce, std::size_t blobCount)
{
    auto tx = std::make_shared<FeeMarketTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().data.to.assign(senderBytes.begin(), senderBytes.end());
    auto const payload = makeBlobSigningPayload(blobCount);
    tx->mutableInner().extraTransactionBytes.assign(payload.begin(), payload.end());
    // A 65-byte r||s||yParity signature; validity is irrelevant here (no ecrecover on this
    // path), but reassembleWeb3RawTransaction requires the wire length.
    bcos::bytes signature(65, 0);
    signature[31] = 0x12;
    signature[63] = 0x34;
    signature[64] = 0x01;
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    for (std::size_t i = 0; i < blobCount; ++i)
    {
        tx->mutableInner().data.blobVersionedHashes.emplace_back(32, static_cast<char>(0x30 + i));
    }
    tx->setNonce(std::to_string(nonce));
    tx->forceSender(rawBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    return tx;
}

BOOST_AUTO_TEST_SUITE(MemPoolFeeMarketTest)

BOOST_AUTO_TEST_CASE(l2_default_config_unchanged)
{
    MemPoolImpl pool;
    BOOST_CHECK(pool.config().chainKind == ChainKind::L2);
    BOOST_CHECK_EQUAL(pool.config().capacity, 0);
    BOOST_CHECK_EQUAL(pool.config().txLifetimeMs, 0);
}

BOOST_AUTO_TEST_CASE(l1_admits_blob_transaction)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("aaaaaaaaaaaaaaaaaaaa", 20);
    auto tx = makeBlobTx(sender, 0, 2);

    BOOST_CHECK(pool.tryAdd(tx) == TransactionStatus::None);
    auto fetched = pool.get(std::vector{tx->hash()});
    BOOST_REQUIRE(fetched.front());
    BOOST_CHECK_EQUAL(fetched.front()->blobVersionedHashes().size(), 2);
}

BOOST_AUTO_TEST_CASE(l1_rejects_blob_without_versioned_hashes)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("bbbbbbbbbbbbbbbbbbbb", 20);
    auto tx = makeBlobTx(sender, 0, 0);

    BOOST_CHECK(pool.tryAdd(tx) == TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(l1_rejects_blob_over_max_blobs_per_transaction)
{
    MemPoolImpl pool{
        MemPoolConfig{.chainKind = ChainKind::L1, .maxBlobsPerTransaction = 2}};
    std::string sender("cccccccccccccccccccc", 20);
    BOOST_CHECK(pool.tryAdd(makeBlobTx(sender, 0, 3)) == TransactionStatus::Malformed);
    BOOST_CHECK(pool.tryAdd(makeBlobTx(sender, 0, 2)) == TransactionStatus::None);
}

// EL mode wires a head-timestamp provider: the per-transaction blob limit is the EIP-7840
// schedule evaluated at the chain head's timestamp, re-read on every admission — a fork
// that activates while the process runs takes effect without a restart, and a chain still
// syncing behind wall-clock time is gated by the fork its head is actually on.
BOOST_AUTO_TEST_CASE(blob_limit_resolves_from_head_timestamp_dynamically)
{
    uint64_t headSeconds = 500;  // pre-Cancun
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1,
        .headTimestampSeconds = [&headSeconds] { return headSeconds; },
        .blobForks = protocol::BlobForkTimes{
            .cancunTime = 1000, .pragueTime = 2000, .bpo1Time = 3000, .bpo2Time = 4000}}};
    std::string sender("dddddddddddddddddddd", 20);

    // Pre-Cancun the schedule is the zero entry: maxBlobs 0, no blob transaction admitted.
    BOOST_CHECK_EQUAL(pool.maxBlobsPerTransaction(), 0);
    BOOST_CHECK(pool.tryAdd(makeBlobTx(sender, 0, 1)) == TransactionStatus::Malformed);

    // Cancun (max 6): seven blobs refused, six admitted.
    headSeconds = 1500;
    BOOST_CHECK_EQUAL(pool.maxBlobsPerTransaction(), 6);
    BOOST_CHECK(pool.tryAdd(makeBlobTx(sender, 0, 7)) == TransactionStatus::Malformed);
    BOOST_CHECK(pool.tryAdd(makeBlobTx(sender, 0, 6)) == TransactionStatus::None);

    // The head crossing the Prague boundary raises the limit to 9 mid-process: the same
    // seven-blob shape that was malformed under Cancun now goes in.
    headSeconds = 2500;
    BOOST_CHECK_EQUAL(pool.maxBlobsPerTransaction(), 9);
    BOOST_CHECK(pool.tryAdd(makeBlobTx(sender, 1, 7)) == TransactionStatus::None);

    // BPO2 (max 21), same mechanism.
    headSeconds = 4500;
    BOOST_CHECK_EQUAL(pool.maxBlobsPerTransaction(), 21);
}

BOOST_AUTO_TEST_CASE(seal_orders_by_effective_tip_across_senders)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string senderA("AAAAAAAAAAAAAAAAAAAA", 20);
    std::string senderB("BBBBBBBBBBBBBBBBBBBB", 20);
    u256 const baseFee = 50;
    // A0: effective tip min(10, 100-50) = 10. B0: min(5, 1000-50) = 5. B1's raw tip is
    // the highest of all, but it sits behind B0 in B's gapless prefix, so the order must
    // be A0, B0, B1 — the fee market picks between sender heads, never inside a sender.
    pool.add(std::vector{makeFeeTx(senderB, 1, 100, 1000), makeFeeTx(senderA, 0, 10, 100),
        makeFeeTx(senderB, 0, 5, 1000)});

    FeeMarketStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(100, state, std::back_inserter(out), baseFee);

    BOOST_REQUIRE_EQUAL(out.size(), 3);
    BOOST_CHECK_EQUAL(std::string(out[0]->sender()), senderA);
    BOOST_CHECK_EQUAL(std::string(out[0]->nonce()), "0");
    BOOST_CHECK_EQUAL(std::string(out[1]->sender()), senderB);
    BOOST_CHECK_EQUAL(std::string(out[1]->nonce()), "0");
    BOOST_CHECK_EQUAL(std::string(out[2]->sender()), senderB);
    BOOST_CHECK_EQUAL(std::string(out[2]->nonce()), "1");
}

BOOST_AUTO_TEST_CASE(seal_fee_cap_below_base_fee_sorts_last_but_is_kept)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string senderA("DDDDDDDDDDDDDDDDDDDD", 20);
    std::string senderC("EEEEEEEEEEEEEEEEEEEE", 20);
    u256 const baseFee = 50;
    pool.add(std::vector{makeFeeTx(senderC, 0, 5, 40), makeFeeTx(senderA, 0, 10, 100)});

    FeeMarketStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(100, state, std::back_inserter(out), baseFee);

    BOOST_REQUIRE_EQUAL(out.size(), 2);
    BOOST_CHECK_EQUAL(std::string(out[0]->sender()), senderA);
    BOOST_CHECK_EQUAL(std::string(out[1]->sender()), senderC);
}

BOOST_AUTO_TEST_CASE(seal_nonce_gap_stops_sender_regardless_of_tip)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string senderA("FFFFFFFFFFFFFFFFFFFF", 20);
    std::string senderB("GGGGGGGGGGGGGGGGGGGG", 20);
    u256 const baseFee = 1;
    // A has a nonce gap at 0: its (huge-tip) nonce-1 transaction is not executable, so B0
    // is the only sealable transaction.
    pool.add(std::vector{makeFeeTx(senderA, 1, 1000, 1000), makeFeeTx(senderB, 0, 1, 10)});

    FeeMarketStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(100, state, std::back_inserter(out), baseFee);

    BOOST_REQUIRE_EQUAL(out.size(), 1);
    BOOST_CHECK_EQUAL(std::string(out[0]->sender()), senderB);
}

BOOST_AUTO_TEST_CASE(seal_respects_account_nonce_from_state)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("HHHHHHHHHHHHHHHHHHHH", 20);
    pool.add(std::vector{makeFeeTx(sender, 0, 100, 100), makeFeeTx(sender, 1, 10, 100)});

    FeeMarketStateStorage state{};
    evmc_address addr{};
    std::copy_n(sender.begin(), sizeof(addr.bytes), addr.bytes);
    ledger::account::EVMAccount account(
        state, addr, ledger::account::nodeAddressTableMode());
    task::syncWait(account.setNonce("1"));

    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(100, state, std::back_inserter(out), u256(1));

    BOOST_REQUIRE_EQUAL(out.size(), 1);
    BOOST_CHECK_EQUAL(std::string(out[0]->nonce()), "1");
}

BOOST_AUTO_TEST_CASE(l1_replacement_requires_ten_percent_bump)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("IIIIIIIIIIIIIIIIIIII", 20);
    auto stored = makeFeeTx(sender, 0, 100, 200);
    pool.add(std::vector{stored});

    // Tip bumped by only 9%: the stored transaction keeps the slot.
    auto weakBump = makeFeeTx(sender, 0, 109, 220);
    pool.add(std::vector{weakBump});
    auto fetched = pool.get(std::vector{stored->hash(), weakBump->hash()});
    BOOST_CHECK(fetched[0]);
    BOOST_CHECK(!fetched[1]);

    // Exactly +10% on both caps: replaces.
    auto fullBump = makeFeeTx(sender, 0, 110, 220);
    pool.add(std::vector{fullBump});
    fetched = pool.get(std::vector{stored->hash(), fullBump->hash()});
    BOOST_CHECK(!fetched[0]);
    BOOST_CHECK(fetched[1]);
}

BOOST_AUTO_TEST_CASE(l1_replacement_checks_both_caps)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("JJJJJJJJJJJJJJJJJJJJ", 20);
    auto stored = makeFeeTx(sender, 0, 100, 200);
    pool.add(std::vector{stored});

    // Tip bumped, fee cap not: still underpriced.
    auto capNotBumped = makeFeeTx(sender, 0, 150, 200);
    pool.add(std::vector{capNotBumped});
    auto fetched = pool.get(std::vector{stored->hash(), capNotBumped->hash()});
    BOOST_CHECK(fetched[0]);
    BOOST_CHECK(!fetched[1]);
}

BOOST_AUTO_TEST_CASE(l2_replacement_still_unconditional)
{
    MemPoolImpl pool;  // L2 default
    std::string sender("KKKKKKKKKKKKKKKKKKKK", 20);
    auto stored = makeFeeTx(sender, 0, 100, 200);
    pool.add(std::vector{stored});
    auto cheaper = makeFeeTx(sender, 0, 1, 1);
    pool.add(std::vector{cheaper});

    auto fetched = pool.get(std::vector{stored->hash(), cheaper->hash()});
    BOOST_CHECK(!fetched[0]);
    BOOST_CHECK(fetched[1]);
}

BOOST_AUTO_TEST_CASE(capacity_evicts_cheapest_transaction)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1, .capacity = 2}};
    std::string senderA("LLLLLLLLLLLLLLLLLLLL", 20);
    std::string senderB("MMMMMMMMMMMMMMMMMMMM", 20);
    std::string senderC("NNNNNNNNNNNNNNNNNNNN", 20);
    auto cheap = makeFeeTx(senderA, 0, 1, 100);
    auto mid = makeFeeTx(senderB, 0, 2, 100);
    auto dear = makeFeeTx(senderC, 0, 3, 100);
    pool.add(std::vector{cheap, mid, dear});

    auto fetched = pool.get(std::vector{cheap->hash(), mid->hash(), dear->hash()});
    BOOST_CHECK(!fetched[0]);  // the cheapest is evicted when the pool overflows
    BOOST_CHECK(fetched[1]);
    BOOST_CHECK(fetched[2]);
}

BOOST_AUTO_TEST_CASE(lifetime_evicts_expired_transactions_on_seal)
{
    MemPoolImpl pool{
        MemPoolConfig{.chainKind = ChainKind::L1, .txLifetimeMs = 60 * 1000}};
    std::string senderA("OOOOOOOOOOOOOOOOOOOO", 20);
    std::string senderB("PPPPPPPPPPPPPPPPPPPP", 20);
    auto stale = makeFeeTx(senderA, 0, 100, 100);
    stale->setImportTime(static_cast<int64_t>(utcTime()) - 10 * 60 * 1000);
    auto fresh = makeFeeTx(senderB, 0, 1, 100);
    pool.add(std::vector{stale, fresh});

    FeeMarketStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(100, state, std::back_inserter(out), u256(1));

    BOOST_REQUIRE_EQUAL(out.size(), 1);
    BOOST_CHECK_EQUAL(out[0]->hash(), fresh->hash());
    auto fetched = pool.get(std::vector{stale->hash()});
    BOOST_CHECK(!fetched[0]);
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// EIP-4844 blob sidecars: registration, query, eviction coupling, versioned-hash lookup
// ---------------------------------------------------------------------------

/// A real KZG sidecar for @p blobCount blobs: genuine commitments and proofs, so the
/// versioned-hash lookup hashes the same values an L1 client would.
static engine::BlobTxSidecar makeSidecar(std::size_t blobCount)
{
    engine::BlobTxSidecar sidecar;
    for (std::size_t i = 0; i < blobCount; ++i)
    {
        bcos::bytes blob(crypto::kzg::BlobSize, bcos::byte{0});
        blob[0] = bcos::byte{static_cast<uint8_t>(i + 1)};
        bcos::bytes commitment, proof;
        BOOST_REQUIRE(crypto::kzg::blobToKzgCommitment(bcos::ref(blob), commitment));
        BOOST_REQUIRE(crypto::kzg::computeBlobKzgProof(bcos::ref(blob), bcos::ref(commitment), proof));
        sidecar.blobs.push_back(std::move(blob));
        sidecar.commitments.push_back(std::move(commitment));
        sidecar.proofs.push_back(std::move(proof));
    }
    return sidecar;
}

BOOST_AUTO_TEST_SUITE(MemPoolBlobSidecarTest)

BOOST_AUTO_TEST_CASE(sidecar_registers_atomically_and_is_queryable)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("saSaSaSaSaSaSaSaSaSa", 20);
    auto tx = makeBlobTx(sender, 0, 1);
    auto const hash = tx->hash();
    auto sidecar = makeSidecar(1);
    auto const commitment = sidecar.commitments.front();

    BOOST_CHECK(pool.tryAdd(std::move(tx), std::move(sidecar)) == TransactionStatus::None);
    auto fetched = pool.blobSidecar(hash);
    BOOST_REQUIRE(fetched.has_value());
    BOOST_REQUIRE_EQUAL(fetched->commitments.size(), 1);
    BOOST_CHECK(fetched->commitments.front() == commitment);
    BOOST_CHECK_EQUAL(fetched->blobs.front().size(), crypto::kzg::BlobSize);
}

BOOST_AUTO_TEST_CASE(plain_blob_transaction_has_no_sidecar)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("sbSbSbSbSbSbSbSbSbSb", 20);
    auto tx = makeBlobTx(sender, 0, 1);
    auto const hash = tx->hash();
    BOOST_CHECK(pool.tryAdd(std::move(tx)) == TransactionStatus::None);
    BOOST_CHECK(!pool.blobSidecar(hash).has_value());
}

BOOST_AUTO_TEST_CASE(sidecar_dropped_with_its_transaction)
{
    // Capacity eviction: the pool holds one transaction, so admitting a second evicts the
    // cheapest — the blob transaction (its static tip key is 0) — and its sidecar with it.
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1, .capacity = 1}};
    std::string senderA("scScScScScScScScScSA", 20);
    std::string senderB("scScScScScScScScScSB", 20);
    auto blobTx = makeBlobTx(senderA, 0, 1);
    auto const blobHash = blobTx->hash();
    BOOST_CHECK(pool.tryAdd(std::move(blobTx), makeSidecar(1)) == TransactionStatus::None);
    BOOST_CHECK(pool.tryAdd(makeFeeTx(senderB, 0, 1, 100)) == TransactionStatus::None);

    BOOST_CHECK(!pool.blobSidecar(blobHash).has_value());

    // Hash removal (the OP build path's removeByHash) clears the sidecar too.
    MemPoolImpl pool2{MemPoolConfig{.chainKind = ChainKind::L1}};
    auto blobTx2 = makeBlobTx(senderA, 0, 1);
    auto const blobHash2 = blobTx2->hash();
    BOOST_CHECK(pool2.tryAdd(std::move(blobTx2), makeSidecar(1)) == TransactionStatus::None);
    pool2.removeByHash(std::vector{blobHash2});
    BOOST_CHECK(!pool2.blobSidecar(blobHash2).has_value());
}

BOOST_AUTO_TEST_CASE(sidecar_dropped_on_expiry)
{
    MemPoolImpl pool{
        MemPoolConfig{.chainKind = ChainKind::L1, .txLifetimeMs = 60 * 1000}};
    std::string sender("sdSdSdSdSdSdSdSdSdSd", 20);
    auto stale = makeBlobTx(sender, 0, 1);
    auto const staleHash = stale->hash();
    stale->setImportTime(static_cast<int64_t>(utcTime()) - 10 * 60 * 1000);
    BOOST_CHECK(pool.tryAdd(std::move(stale), makeSidecar(1)) == TransactionStatus::None);

    // A second admission runs the expiry eviction and takes the sidecar down with the tx.
    std::string senderB("seSeSeSeSeSeSeSeSeSe", 20);
    BOOST_CHECK(pool.tryAdd(makeFeeTx(senderB, 0, 1, 100)) == TransactionStatus::None);
    BOOST_CHECK(!pool.blobSidecar(staleHash).has_value());
}

BOOST_AUTO_TEST_CASE(blobs_by_versioned_hashes)
{
    MemPoolImpl pool{MemPoolConfig{.chainKind = ChainKind::L1}};
    std::string sender("sfSfSfSfSfSfSfSfSfSf", 20);
    auto tx = makeBlobTx(sender, 0, 2);
    auto sidecar = makeSidecar(2);
    std::vector<crypto::HashType> versionedHashes;
    for (auto const& commitment : sidecar.commitments)
    {
        versionedHashes.push_back(
            crypto::kzg::versionedHashFromCommitment(bcos::ref(commitment)));
    }
    auto const blobs = sidecar.blobs;
    auto const proofs = sidecar.proofs;
    BOOST_CHECK(pool.tryAdd(std::move(tx), std::move(sidecar)) == TransactionStatus::None);

    crypto::HashType const unknown("0x9999999999999999999999999999999999999999999999999999999999999999");
    std::vector<crypto::HashType> query{versionedHashes[0], unknown, versionedHashes[1]};
    auto items = pool.blobsByVersionedHashes(query);
    BOOST_REQUIRE_EQUAL(items.size(), 3);
    BOOST_REQUIRE(items[0].has_value());
    BOOST_CHECK(!items[1].has_value());
    BOOST_REQUIRE(items[2].has_value());
    BOOST_CHECK(items[0]->blob == blobs[0]);
    BOOST_CHECK(items[0]->proof == proofs[0]);
    BOOST_CHECK(items[2]->blob == blobs[1]);
    // The served pair still verifies under KZG.
    BOOST_CHECK(crypto::kzg::verifyBlobKzgProof(
        bcos::ref(items[0]->blob), bcos::ref(items[0]->commitment), bcos::ref(items[0]->proof)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
