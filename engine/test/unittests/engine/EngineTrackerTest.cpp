/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/EngineServiceCommon.h"
#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/PayloadCache.h"

#include "engine/bcos-engine/PayloadId.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

using namespace bcos;
using namespace bcos::engine;

namespace
{

ResolvedForkchoice resolved(
    h256 hash, bcos::protocol::BlockNumber number, bool canonical, bool attributes)
{
    return ResolvedForkchoice{
        .state = ForkchoiceState{hash, hash, hash},
        .headNumber = number,
        .safeNumber = number,
        .finalizedNumber = number,
        .headCanonical = canonical,
        .payloadAttributesPresent = attributes,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
}

BuiltPayloadPtr makePayload(std::uint32_t version, bool withWithdrawalsRoot = false)
{
    auto entry = std::make_shared<BuiltPayload>();
    entry->version = version;
    entry->executionPayload.blockNumber = 1;
    if (withWithdrawalsRoot)
    {
        entry->executionPayload.withdrawalsRoot = h256(42);
    }
    // V3+ getPayload requires the blob-gas pair and beacon root (EngineTracker shape gate).
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3))
    {
        entry->executionPayload.blobGasUsed = u256(0);
        entry->executionPayload.excessBlobGas = u256(0);
        entry->parentBeaconBlockRoot = h256(1);
    }
    return entry;
}

template <typename Exception>
void checkExceptionMessage(auto&& action, const char* expectedMessage)
{
    BOOST_CHECK_EXCEPTION(action(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
}

std::optional<PayloadID> legacyDerivePayloadId(
    PayloadAttributes const& payloadAttributes, h256 const& parentHash, std::uint32_t version)
{
    std::vector<h256> txHashes;
    if (payloadAttributes.transactions.has_value())
    {
        txHashes.reserve(payloadAttributes.transactions->size());
        for (auto const& hexTx : *payloadAttributes.transactions)
        {
            try
            {
                auto raw = bcos::fromHex(hexTx);
                txHashes.emplace_back(bcos::crypto::keccak256Hash(bcos::ref(raw)));
            }
            catch (bcos::BadHexCharacter const&)
            {
                return std::nullopt;
            }
        }
    }
    return bcos::engine::derivePayloadId(
        payloadAttributes, parentHash, txHashes, static_cast<uint8_t>(version));
}

PayloadAttributes minimalPayloadAttributes()
{
    PayloadAttributes attrs;
    attrs.timestamp = 1'700'000'000'000;
    attrs.prevRandao = h256(std::string(64, '2'));
    attrs.suggestedFeeRecipient = bcos::Address(std::string(40, '3'));
    attrs.withdrawals = std::vector<WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = h256(std::string(64, '4'));
    return attrs;
}

struct ThrowingArtifact
{
    int value = 0;
    static inline bool throwOnAssign = false;
    ThrowingArtifact() = default;
    explicit ThrowingArtifact(int v) : value(v) {}
    // Declaring operator= below would delete the implicit copy ctor and suppress
    // the move ctor; publishBuiltPayload() snapshots the whole map (copy) and
    // emplaces node values, so both must exist. Only operator= may throw: the
    // rollback snapshot is taken while throwOnAssign can already be true.
    ThrowingArtifact(ThrowingArtifact const&) = default;
    ThrowingArtifact(ThrowingArtifact&&) noexcept = default;
    ThrowingArtifact& operator=(ThrowingArtifact&& other)
    {
        if (throwOnAssign)
        {
            throw std::runtime_error{"artifact assign failed"};
        }
        value = other.value;
        return *this;
    }
    ThrowingArtifact& operator=(ThrowingArtifact const& other)
    {
        if (throwOnAssign)
        {
            throw std::runtime_error{"artifact assign failed"};
        }
        value = other.value;
        return *this;
    }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(EngineTrackerTest)

BOOST_AUTO_TEST_CASE(payload_cache_keeps_hash_index_and_fifo_consistent)
{
    PayloadCache cache;
    std::vector<PayloadID> ids;
    for (std::uint64_t i = 0; i < 65; ++i)
    {
        auto entry = std::make_shared<BuiltPayload>();
        entry->version = 1;
        entry->executionPayload.blockNumber = i;
        auto id = bcos::toHexStringWithPrefix(
            bcos::bytesConstRef(reinterpret_cast<const byte*>(&i), sizeof(i)));
        ids.push_back(id);
        cache.put(id, h256(i + 1), std::static_pointer_cast<const BuiltPayload>(entry));
    }

    BOOST_CHECK(!cache.find(ids.front()));
    BOOST_CHECK(!cache.payloadIdForHash(h256(1)).has_value());
    BOOST_REQUIRE(cache.find(ids.back()));
    BOOST_REQUIRE(cache.payloadIdForHash(h256(65)).has_value());
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(65)), ids.back());
}

BOOST_AUTO_TEST_CASE(payload_cache_deduplicates_repeated_payload_id)
{
    PayloadCache cache;
    auto entry = std::make_shared<BuiltPayload>();
    entry->version = 1;
    const PayloadID id = "0x0102030405060708";
    for (int i = 0; i < 70; ++i)
    {
        cache.put(id, h256(7), std::static_pointer_cast<const BuiltPayload>(entry));
    }
    BOOST_REQUIRE(cache.find(id));
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(7)), id);
}

BOOST_AUTO_TEST_CASE(payload_cache_replacing_same_id_drops_stale_hash)
{
    PayloadCache cache;
    const PayloadID id = "0xaaaaaaaaaaaaaaaa";
    cache.put(id, h256(1), makePayload(1));
    cache.put(id, h256(2), makePayload(2));
    BOOST_CHECK(!cache.payloadIdForHash(h256(1)).has_value());
    BOOST_REQUIRE(cache.payloadIdForHash(h256(2)).has_value());
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(2)), id);
    BOOST_REQUIRE(cache.find(id));
    BOOST_CHECK_EQUAL(cache.find(id)->version, 2);
}

// Finding F26: a re-published payload (same deterministic id, identical FCU retry)
// refreshes its FIFO position — the oldest unrefreshed entry is evicted first, not the
// freshly re-published one.
BOOST_AUTO_TEST_CASE(payload_cache_reput_refreshes_fifo_position)
{
    PayloadCache cache;
    const PayloadID idA = "0x0101010101010101";
    const PayloadID idB = "0x0202020202020202";
    const PayloadID idC = "0x0303030303030303";
    cache.put(idA, h256(1), makePayload(1));
    cache.put(idB, h256(2), makePayload(2));
    cache.put(idC, h256(3), makePayload(3));

    // Re-put A: with the fix its FIFO slot moves to the tail, so B is now the oldest.
    cache.put(idA, h256(1), makePayload(11));

    // Fill to beyond the 64-entry bound: the FIFO front must be B, not A.
    for (std::uint64_t i = 0; i < 62; ++i)
    {
        auto id = bcos::toHexStringWithPrefix(
            bcos::bytesConstRef(reinterpret_cast<const byte*>(&i), sizeof(i)));
        cache.put(id, h256(i + 10), makePayload(static_cast<std::uint32_t>(i) + 10));
    }

    BOOST_CHECK(!cache.find(idB));
    BOOST_CHECK(!cache.payloadIdForHash(h256(2)).has_value());
    BOOST_REQUIRE(cache.find(idA));
    BOOST_CHECK_EQUAL(cache.find(idA)->version, 11);
    BOOST_REQUIRE(cache.find(idC));
}

BOOST_AUTO_TEST_CASE(payload_cache_put_and_retain_only_clears_intermediate_state)
{
    PayloadCache cache;
    cache.put("0x01", h256(1), makePayload(1));
    cache.put("0x02", h256(2), makePayload(1));
    cache.putAndRetainOnly("0x03", h256(3), makePayload(1));
    BOOST_CHECK(!cache.find("0x01"));
    BOOST_CHECK(!cache.find("0x02"));
    BOOST_REQUIRE(cache.find("0x03"));
}

BOOST_AUTO_TEST_CASE(engine_tracker_put_and_retain_payload)
{
    EngineTracker tracker;
    auto guard = tracker.lockExclusive();
    guard.putPayload("0x01", h256(1), makePayload(1));
    guard.putPayload("0x02", h256(2), makePayload(1));
    guard.putAndRetainPayload("0x03", h256(3), makePayload(1));
    BOOST_CHECK(!guard.findPayload("0x01"));
    BOOST_REQUIRE(guard.findPayload("0x03"));
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_parent_even_with_attributes)
{
    // Matrix: S2 — release EngineServiceImpl never rebuilds on parent; older head is swallowed.
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(9), 9, true, true));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_REQUIRE(tracker.trackedHead().has_value());
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(10));
}

// A set-but-unresolved safe/finalized hash (non-zero hash, nullopt number) is rejected
// fail-closed, and a previous FCU's stored height survives it — the store shares the
// gate's requiresCanonical predicate, so an unresolved pair can never wipe m_safe.
BOOST_AUTO_TEST_CASE(engine_tracker_set_but_unresolved_safe_hash_is_rejected)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    BOOST_REQUIRE(tracker.safeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*tracker.safeBlockNumber(), 10);

    ResolvedForkchoice unresolved{
        .state = ForkchoiceState{h256(11), h256(20), h256(21)},
        .headNumber = 11,
        .safeNumber = std::nullopt,
        .finalizedNumber = std::nullopt,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(unresolved); },
        "could not be resolved");

    // The stored heights survive the rejected apply.
    BOOST_REQUIRE(tracker.safeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*tracker.safeBlockNumber(), 10);
}

// headCanonical is fail-closed on first apply and on +1 advance, matching the
// safe/finalized flags — an unconfirmed head must never seed or advance the tracker.
BOOST_AUTO_TEST_CASE(engine_tracker_head_canonical_required_on_first_apply_and_plus_one)
{
    EngineTracker tracker;
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(resolved(h256(10), 10, false, false)); },
        "head block is not canonical");
    BOOST_CHECK(!tracker.trackedHead().has_value());

    // First apply with a confirmed head seeds the tracker…
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    BOOST_REQUIRE(tracker.trackedHead().has_value());

    // …and a +1 advance still requires the canonical confirmation.
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(resolved(h256(11), 11, false, false)); },
        "head block is not canonical");
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);

    tracker.applyForkchoice(resolved(h256(11), 11, true, false));
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 11);
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_old_head_without_attributes)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(9), 9, true, false));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_noncanonical_parent_even_with_attributes)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(9), 9, false, true));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(10));
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_older_than_parent_with_attributes)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(8), 8, true, true));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(10));
}

BOOST_AUTO_TEST_CASE(engine_tracker_allows_canonical_same_height_reorg_sibling)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(1010), 10, true, false));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Applied);
    BOOST_REQUIRE(tracker.trackedHead().has_value());
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(1010));
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_noncanonical_same_height)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(resolved(h256(1010), 10, false, false)); },
        "Forkchoice head block hash conflicts with tracked block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_safe_above_head)
{
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(1), h256(2), h256(3)},
        .headNumber = 5,
        .safeNumber = 6,
        .finalizedNumber = 4,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice safe block number must not exceed head block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_finalized_above_head)
{
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(1), h256(2), h256(3)},
        .headNumber = 5,
        .safeNumber = 4,
        .finalizedNumber = 6,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice finalized block number must not exceed head block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_finalized_above_safe)
{
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(1), h256(2), h256(3)},
        .headNumber = 10,
        .safeNumber = 5,
        .finalizedNumber = 6,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice finalized block number must not exceed safe block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_head_jump)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(resolved(h256(12), 12, true, false)); },
        "Forkchoice head block number must increase by exactly 1");
}

BOOST_AUTO_TEST_CASE(engine_tracker_updates_safe_and_finalized_on_apply)
{
    EngineTracker tracker;
    ResolvedForkchoice first{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    tracker.applyForkchoice(first);
    BOOST_CHECK_EQUAL(*tracker.safeBlockNumber(), 8);
    BOOST_CHECK_EQUAL(*tracker.finalizedBlockNumber(), 7);
}

BOOST_AUTO_TEST_CASE(engine_tracker_zero_hash_keeps_safe_and_finalized)
{
    // Finding AJ: Engine-API unset (zero) safe/finalized must not clear stored heights.
    EngineTracker tracker;
    ResolvedForkchoice first{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    BOOST_CHECK(tracker.applyForkchoice(first) == ForkchoiceApplyResult::Applied);

    ResolvedForkchoice unset{
        .state = ForkchoiceState{h256(11), h256{}, h256{}},
        .headNumber = 11,
        .safeNumber = 0,
        .finalizedNumber = 0,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    BOOST_CHECK(tracker.applyForkchoice(unset) == ForkchoiceApplyResult::Applied);
    BOOST_REQUIRE(tracker.trackedHead().has_value());
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 11);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(11));
    BOOST_REQUIRE(tracker.safeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*tracker.safeBlockNumber(), 8);
    BOOST_REQUIRE(tracker.finalizedBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*tracker.finalizedBlockNumber(), 7);
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_non_canonical_safe)
{
    // op-geth: "safe block not in canonical chain"
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = false,
        .finalizedCanonical = true,
    };
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(bad); }, "Forkchoice safe block not in canonical chain");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_non_canonical_finalized)
{
    // op-geth: "final block not in canonical chain"
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = false,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice finalized block not in canonical chain");
}

BOOST_AUTO_TEST_CASE(engine_tracker_omitted_canonical_flags_fail_closed)
{
    // Default safeCanonical/finalizedCanonical are false. Omitting them must not apply.
    EngineTracker tracker;
    ResolvedForkchoice omitted{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(omitted); },
        "Forkchoice safe block not in canonical chain");
}

BOOST_AUTO_TEST_CASE(engine_tracker_zero_hash_omitted_canonical_flags_applies)
{
    // Zero hash is Engine-API "not set". The canonical gate must use the same
    // predicate as store (hash != 0), so default-false flags do not reject it.
    EngineTracker tracker;
    ResolvedForkchoice unset{
        .state = ForkchoiceState{h256(10), h256{}, h256{}},
        .headNumber = 10,
        .safeNumber = 0,
        .finalizedNumber = 0,
        .headCanonical = true,
        .payloadAttributesPresent = false,
    };
    BOOST_CHECK(tracker.applyForkchoice(unset) == ForkchoiceApplyResult::Applied);
    BOOST_CHECK(!tracker.safeBlockNumber().has_value());
    BOOST_CHECK(!tracker.finalizedBlockNumber().has_value());
}

BOOST_AUTO_TEST_CASE(engine_tracker_allows_safe_finalized_number_rewind)
{
    // op-geth SetSafe/SetFinalized overwrite; lower numbers are legal when canonical.
    EngineTracker tracker;
    ResolvedForkchoice first{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    tracker.applyForkchoice(first);
    ResolvedForkchoice rewind{
        .state = ForkchoiceState{h256(11), h256(13), h256(14)},
        .headNumber = 11,
        .safeNumber = 6,
        .finalizedNumber = 5,
        .headCanonical = true,
        .payloadAttributesPresent = false,
        .safeCanonical = true,
        .finalizedCanonical = true,
    };
    BOOST_CHECK(tracker.applyForkchoice(rewind) == ForkchoiceApplyResult::Applied);
    BOOST_CHECK_EQUAL(*tracker.safeBlockNumber(), 6);
    BOOST_CHECK_EQUAL(*tracker.finalizedBlockNumber(), 5);
}

BOOST_AUTO_TEST_CASE(engine_tracker_moved_from_exclusive_access_is_dead)
{
    EngineTracker tracker;
    auto guard = tracker.lockExclusive();
    auto moved = std::move(guard);
    checkExceptionMessage<InvalidGuardState>(
        [&]() { guard.findPayload("0x01"); }, "used after move");
    BOOST_CHECK(moved.findPayload("0x01") == nullptr);
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_unknown)
{
    EngineTracker tracker;
    checkExceptionMessage<UnknownPayload>(
        [&]() { tracker.getPayload("0xdeadbeef", 1); }, "Unknown payload");
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_unsupported_version)
{
    EngineTracker tracker;
    checkExceptionMessage<UnsupportedEngineApiVersion>(
        [&]() { tracker.getPayload("0xdeadbeef", 0); }, "Unsupported Engine API version");
    checkExceptionMessage<UnsupportedEngineApiVersion>(
        [&]() { tracker.getPayload("0xdeadbeef", 6); }, "Unsupported Engine API version");
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_incompatible_version)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    auto guard = tracker.lockExclusive();
    guard.putPayload(id, h256(7), makePayload(4));
    guard = EngineTracker::ExclusiveAccess{};

    checkExceptionMessage<IncompatiblePayloadVersion>([&]() { tracker.getPayload(id, 1); },
        "Payload version is incompatible with requested method version");
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v3_requires_blob_and_beacon)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto entry = std::make_shared<BuiltPayload>();
        entry->version = 3;
        entry->executionPayload.blockNumber = 1;
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), entry);
    }
    checkExceptionMessage<IncompatiblePayloadVersion>(
        [&]() { tracker.getPayload(id, 3); }, "Payload does not carry the V3+ response shape");
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v4_requires_withdrawals_root)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), makePayload(3));
    }
    checkExceptionMessage<IncompatiblePayloadVersion>(
        [&]() { tracker.getPayload(id, 4); }, "Payload does not carry the V4+ response shape");
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v4_includes_execution_requests)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), makePayload(3, true));
    }
    auto result = tracker.getPayload(id, 4);
    BOOST_REQUIRE(result);
    BOOST_REQUIRE(result->executionRequests.has_value());
    BOOST_CHECK(result->executionRequests->empty());
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v3_omits_execution_requests)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), makePayload(3, true));
    }
    auto result = tracker.getPayload(id, 3);
    BOOST_REQUIRE(result);
    BOOST_CHECK(!result->executionRequests.has_value());
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_survives_cache_eviction)
{
    // Finding AF: getPayload copies from the shared_ptr; evicting the cache
    // entry after the call must not empty the returned tx raw.
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    const PayloadID other = "0x1111111111111111";
    {
        auto entry = std::make_shared<BuiltPayload>(*makePayload(3, true));
        entry->executionPayload.transactions.push_back(
            EngineTransaction{.raw = bytes{0x7e, 0xfa, 0xce}, .decoded = nullptr});
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), std::move(entry));
    }
    auto result = tracker.getPayload(id, 3);
    {
        auto otherEntry = makePayload(3, true);
        auto guard = tracker.lockExclusive();
        guard.putAndRetainPayload(other, h256(8), std::move(otherEntry));
    }
    BOOST_REQUIRE(result);
    BOOST_REQUIRE_EQUAL(result->executionPayload.transactions.size(), 1);
    BOOST_CHECK(result->executionPayload.transactions[0].raw == (bytes{0x7e, 0xfa, 0xce}));
    BOOST_CHECK_THROW(tracker.getPayload(id, 3), UnknownPayload);
}

BOOST_AUTO_TEST_CASE(engine_tracker_exclusive_guard_exposes_payload_cache)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    const PayloadID id = "0x0102030405060708";
    auto guard = tracker.lockExclusive();
    guard.putPayload(id, h256(10), makePayload(1));
    BOOST_REQUIRE(guard.findPayload(id));
    BOOST_REQUIRE(guard.payloadIdForHash(h256(10)).has_value());
    BOOST_CHECK_EQUAL(guard.forkchoiceState().headBlockHash, h256(10));
}

BOOST_AUTO_TEST_CASE(engine_tracker_shared_guard_allows_concurrent_readers)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(10), makePayload(1));
    }

    std::atomic<int> activeReaders{0};
    std::atomic<int> peakReaders{0};
    std::atomic<bool> readerDataOk{true};
    std::barrier sync(3);

    auto read = [&]() {
        bool localOk = true;
        auto guard = tracker.lockShared();
        if (!guard.findPayload(id) || guard.forkchoiceState().headBlockHash != h256(10))
        {
            localOk = false;
        }
        else
        {
            int current = ++activeReaders;
            int observed = peakReaders.load();
            while (observed < current && !peakReaders.compare_exchange_weak(observed, current))
            {
            }
        }

        sync.arrive_and_wait();
        sync.arrive_and_wait();

        if (localOk)
        {
            --activeReaders;
        }
        else
        {
            readerDataOk = false;
        }
    };

    std::thread t1(read);
    std::thread t2(read);
    sync.arrive_and_wait();
    sync.arrive_and_wait();
    t1.join();
    t2.join();

    BOOST_CHECK(readerDataOk.load());
    BOOST_CHECK_GE(peakReaders.load(), 2);
    BOOST_CHECK_EQUAL(activeReaders.load(), 0);
}

BOOST_AUTO_TEST_CASE(engine_tracker_exclusive_guard_blocks_shared_readers)
{
    // Observability boundary: std::shared_mutex exposes no API to observe a thread
    // blocked inside lock_shared(). This test establishes happens-before with explicit
    // permission to attempt lockShared():
    //   permissionGranted -> permissionConsumed (worker consumed the permit and proceeds
    //   to lockShared()) -> lockAcquired only after exclusive release.
    // permissionConsumed + acquired==false while exclusive is held excludes "thread never
    // ran"; acquired==true after release excludes "no blocking occurred".

    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));

    std::mutex syncMutex;
    std::condition_variable workerReadyCv;
    std::condition_variable permissionCv;
    std::condition_variable acquiredCv;

    bool workerReady = false;
    bool permissionGranted = false;
    bool permissionConsumed = false;
    bool lockAcquired = false;

    struct Outcome
    {
        bool workerReadySeen = false;
        bool permissionConsumedWhileExclusive = false;
        bool notAcquiredWhileExclusive = false;
        bool acquiredAfterRelease = false;
    } outcome;
    constexpr auto c_timeout = std::chrono::seconds(2);

    auto guard = tracker.lockExclusive();

    std::thread worker([&] {
        {
            std::lock_guard lock(syncMutex);
            workerReady = true;
        }
        workerReadyCv.notify_one();

        {
            std::unique_lock lock(syncMutex);
            permissionCv.wait(lock, [&]() { return permissionGranted; });
            permissionConsumed = true;
        }
        permissionCv.notify_one();

        (void)tracker.lockShared();

        {
            std::lock_guard lock(syncMutex);
            lockAcquired = true;
        }
        acquiredCv.notify_one();
    });

    {
        std::unique_lock lock(syncMutex);
        if (workerReadyCv.wait_for(lock, c_timeout, [&]() { return workerReady; }))
        {
            outcome.workerReadySeen = true;
            permissionGranted = true;
            permissionCv.notify_one();
            if (permissionCv.wait_for(lock, c_timeout, [&]() { return permissionConsumed; }))
            {
                outcome.permissionConsumedWhileExclusive = true;
                outcome.notAcquiredWhileExclusive = !lockAcquired;
            }
        }
    }

    {
        std::lock_guard lock(syncMutex);
        permissionGranted = true;
    }
    permissionCv.notify_all();
    guard = EngineTracker::ExclusiveAccess{};

    {
        std::unique_lock lock(syncMutex);
        if (acquiredCv.wait_for(lock, c_timeout, [&]() { return lockAcquired; }))
        {
            outcome.acquiredAfterRelease = lockAcquired;
        }
    }

    worker.join();

    BOOST_CHECK(outcome.workerReadySeen);
    BOOST_CHECK(outcome.permissionConsumedWhileExclusive);
    BOOST_CHECK(outcome.notAcquiredWhileExclusive);
    BOOST_CHECK(outcome.acquiredAfterRelease);
}

BOOST_AUTO_TEST_CASE(engine_tracker_retain_only_through_guard)
{
    EngineTracker tracker;
    const PayloadID keep = "0xkeep";
    const PayloadID drop = "0xdrop";
    auto guard = tracker.lockExclusive();
    guard.putPayload(keep, h256(1), makePayload(1));
    guard.putPayload(drop, h256(2), makePayload(1));
    guard.retainOnly(keep, h256(1));
    BOOST_REQUIRE(guard.findPayload(keep));
    BOOST_CHECK(!guard.findPayload(drop));
    BOOST_CHECK(!guard.payloadIdForHash(h256(2)).has_value());
}

BOOST_AUTO_TEST_CASE(payload_cache_put_and_retain_reports_dropped_ids)
{
    PayloadCache cache;
    cache.put("0xa", h256(1), makePayload(1));
    cache.put("0xb", h256(2), makePayload(1));
    cache.put("0xc", h256(3), makePayload(1));
    auto result = cache.putAndRetainOnly("0xd", h256(4), makePayload(1));
    std::vector<PayloadID> evicted = result.evicted;
    std::sort(evicted.begin(), evicted.end());
    std::vector<PayloadID> const expected{"0xa", "0xb", "0xc"};
    BOOST_CHECK(evicted == expected);
    BOOST_REQUIRE(cache.find("0xd"));
    BOOST_CHECK(!cache.find("0xa"));
    BOOST_CHECK(!cache.find("0xb"));
    BOOST_CHECK(!cache.find("0xc"));
}

BOOST_AUTO_TEST_CASE(publish_built_payload_restores_replaced_entry_on_artifacts_throw)
{
    // Finding A: put overwrites the same id; artifacts assign then throws.
    // erasePayload(id) would drop both the new and the previous entry.
    ThrowingArtifact::throwOnAssign = false;

    EngineTracker tracker;
    auto guard = tracker.lockExclusive();
    const PayloadID id = "0xsameid";
    guard.putPayload(id, h256(1), makePayload(1));

    std::unordered_map<PayloadID, ThrowingArtifact> artifacts;
    artifacts.emplace(id, ThrowingArtifact{42});
    ThrowingArtifact::throwOnAssign = true;

    BOOST_CHECK_THROW(
        publishBuiltPayload(guard, artifacts, id, h256(2), makePayload(2), ThrowingArtifact{7}),
        std::runtime_error);

    ThrowingArtifact::throwOnAssign = false;
    BOOST_REQUIRE(guard.findPayload(id));
    BOOST_CHECK_EQUAL(guard.findPayload(id)->version, 1);
    BOOST_REQUIRE(guard.payloadIdForHash(h256(1)).has_value());
    BOOST_CHECK_EQUAL(*guard.payloadIdForHash(h256(1)), id);
    BOOST_CHECK(!guard.payloadIdForHash(h256(2)).has_value());
    BOOST_REQUIRE(artifacts.contains(id));
    BOOST_CHECK_EQUAL(artifacts.at(id).value, 42);
}

BOOST_AUTO_TEST_CASE(engine_common_payload_version_matrix_gold)
{
    // GetPayloadVn window (op-geth): V1 only PayloadV1; V2 accepts <=2; V3/V4/V5 only PayloadV3.
    constexpr bool gold[5][4] = {
        {true, false, false, false},  // request V1
        {true, true, false, false},   // request V2
        {false, false, true, false},  // request V3
        {false, false, true, false},  // request V4
        {false, false, true, false},  // request V5
    };
    for (std::uint32_t request = 1; request <= 5; ++request)
    {
        for (std::uint32_t built = 1; built <= 4; ++built)
        {
            BOOST_CHECK_EQUAL(engine_common::isGetPayloadVersionCompatible(
                                  static_cast<ApiVersion>(request), built),
                gold[request - 1][built - 1]);
        }
    }
}

BOOST_AUTO_TEST_CASE(engine_common_capabilities_gold)
{
    auto const caps = engine_common::supportedCapabilities();
    std::vector<std::string> const gold{"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV1", "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
    BOOST_CHECK(caps == gold);
}

BOOST_AUTO_TEST_CASE(engine_common_validate_payload_attributes_gold)
{
    auto expectError = [](std::optional<std::string> const& error, char const* needle) {
        BOOST_REQUIRE(error.has_value());
        BOOST_CHECK(error->find(needle) != std::string::npos);
    };

    auto attrs = minimalPayloadAttributes();
    expectError(engine_common::validatePayloadAttributes(attrs, 1), "PayloadAttributesV1");
    expectError(engine_common::validatePayloadAttributes(attrs, 2), "parentBeaconBlockRoot");
    BOOST_CHECK(!engine_common::validatePayloadAttributes(attrs, 3));
    BOOST_CHECK(!engine_common::validatePayloadAttributes(attrs, 4));

    PayloadAttributes missingBeacon = minimalPayloadAttributes();
    missingBeacon.parentBeaconBlockRoot = std::nullopt;
    expectError(
        engine_common::validatePayloadAttributes(missingBeacon, 3), "parentBeaconBlockRoot");
    expectError(
        engine_common::validatePayloadAttributes(missingBeacon, 4), "parentBeaconBlockRoot");

    PayloadAttributes badHex = minimalPayloadAttributes();
    badHex.transactions = std::vector<std::string>{"0xZZ"};
    expectError(engine_common::validatePayloadAttributes(badHex, 3), "is not a hex string");

    PayloadAttributes nonEmptyWithdrawals = minimalPayloadAttributes();
    nonEmptyWithdrawals.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 1, .validatorIndex = 2, .amount = 3, .address = bcos::Address{}}};
    expectError(
        engine_common::validatePayloadAttributes(nonEmptyWithdrawals, 3), "non-empty withdrawals");

    PayloadAttributes missingWithdrawals = minimalPayloadAttributes();
    missingWithdrawals.withdrawals = std::nullopt;
    missingWithdrawals.parentBeaconBlockRoot = std::nullopt;
    expectError(engine_common::validatePayloadAttributes(missingWithdrawals, 2),
        "withdrawals are required");

    PayloadAttributes eip1559OnV2 = minimalPayloadAttributes();
    eip1559OnV2.parentBeaconBlockRoot = std::nullopt;
    eip1559OnV2.eip1559Params = bcos::bytes(8, 0);
    expectError(engine_common::validatePayloadAttributes(eip1559OnV2, 2), "eip1559Params");

    PayloadAttributes minBaseFeeAlone = minimalPayloadAttributes();
    minBaseFeeAlone.minBaseFee = 0;
    expectError(engine_common::validatePayloadAttributes(minBaseFeeAlone, 3), "eip1559Params");

    PayloadAttributes badEip1559Len = minimalPayloadAttributes();
    badEip1559Len.eip1559Params = bcos::bytes(7, 1);
    expectError(engine_common::validatePayloadAttributes(badEip1559Len, 3), "exactly 8 bytes");

    PayloadAttributes mixedZeroEip1559 = minimalPayloadAttributes();
    mixedZeroEip1559.eip1559Params = bcos::fromHex("000000fa00000000");
    expectError(engine_common::validatePayloadAttributes(mixedZeroEip1559, 3),
        "both zero or both non-zero");

    PayloadAttributes blobForced = minimalPayloadAttributes();
    blobForced.transactions = std::vector<std::string>{"0x03aabb"};
    expectError(engine_common::validatePayloadAttributes(blobForced, 3), "blob transactions");

    PayloadAttributes tooManyForced = minimalPayloadAttributes();
    tooManyForced.transactions =
        std::vector<std::string>(engine_common::c_maxForcedTxCount + 1, "0x00");
    expectError(engine_common::validatePayloadAttributes(tooManyForced, 3), "count ceiling");

    PayloadAttributes tooManyForcedBytes = minimalPayloadAttributes();
    tooManyForcedBytes.transactions = std::vector<std::string>{
        "0x" + std::string(2 * (engine_common::c_maxForcedTxBytes + 1), 'a')};
    expectError(engine_common::validatePayloadAttributes(tooManyForcedBytes, 3), "byte ceiling");

    // Finding BY: hex-length estimate rejects before fromHex. Second tx alone
    // is over the ceiling; first stays tiny so a running-total-after-decode
    // implementation would still allocate the second body.
    PayloadAttributes secondTxOverCeiling = minimalPayloadAttributes();
    secondTxOverCeiling.transactions = std::vector<std::string>{
        "0x7e00", "0x" + std::string(2 * (engine_common::c_maxForcedTxBytes + 1), 'a')};
    expectError(engine_common::validatePayloadAttributes(secondTxOverCeiling, 3), "byte ceiling");

    BOOST_CHECK_EQUAL(engine_common::decodedHexByteCount("0x"), 0);
    BOOST_CHECK_EQUAL(engine_common::decodedHexByteCount("0x00"), 1);
    BOOST_CHECK_EQUAL(engine_common::decodedHexByteCount("aaa"), 2);
}

BOOST_AUTO_TEST_CASE(engine_common_derive_payload_id_requires_decoded_forced_txs)
{
    // Hex fallback is gone: attributes with transactions and an empty decoded
    // span return nullopt. Pre-decoded bodies still match PayloadId.h.
    const h256 parentHash = h256(std::string(64, '1'));
    auto attrs = minimalPayloadAttributes();
    attrs.transactions = std::vector<std::string>{"0x7eface"};
    BOOST_CHECK(!engine_common::derivePayloadId(attrs, parentHash, 3).has_value());

    auto decoded = std::vector<bcos::bytes>{bcos::fromHex("0x7eface")};
    BOOST_CHECK(engine_common::derivePayloadId(attrs, parentHash, 3, decoded) ==
                legacyDerivePayloadId(attrs, parentHash, 3));
}

BOOST_AUTO_TEST_CASE(engine_common_derive_payload_id_matches_legacy)
{
    const h256 parentHash = h256(std::string(64, '1'));
    auto attrs = minimalPayloadAttributes();
    for (std::uint32_t version = 1; version <= 4; ++version)
    {
        BOOST_CHECK(engine_common::derivePayloadId(attrs, parentHash, version) ==
                    legacyDerivePayloadId(attrs, parentHash, version));
    }

    attrs.transactions = std::vector<std::string>{"0xZZ"};
    BOOST_CHECK(!engine_common::derivePayloadId(attrs, parentHash, 3).has_value());
    BOOST_CHECK(!legacyDerivePayloadId(attrs, parentHash, 3).has_value());
}

BOOST_AUTO_TEST_CASE(compare_with_built_payload_pins_each_hash_field)
{
    auto filled = []() {
        ExecutionPayload payload;
        payload.extraData = bytes{0x01, 0x02};
        payload.parentHash = h256(1);
        payload.stateRoot = h256(2);
        payload.receiptsRoot = h256(3);
        payload.logsBloom.fill(0x11);
        payload.prevRandao = h256(4);
        payload.gasLimit = 5;
        payload.gasUsed = 6;
        payload.baseFeePerGas = 7;
        payload.blockHash = h256(8);
        payload.feeRecipient = Address(9);
        payload.timestamp = 10;
        payload.blockNumber = 11;
        payload.transactions.push_back(EngineTransaction{.raw = bytes{0xaa}, .decoded = nullptr});
        payload.withdrawalsRoot = h256(12);
        payload.blobGasUsed = u256(13);
        payload.excessBlobGas = u256(14);
        return payload;
    };
    auto const built = filled();
    BOOST_CHECK(!bcos::engine::detail::compareWithBuiltPayload(built, built).has_value());

    auto expectField = [&](auto&& mutate, char const* field) {
        auto submitted = filled();
        mutate(submitted);
        auto error = bcos::engine::detail::compareWithBuiltPayload(submitted, built);
        BOOST_REQUIRE(error.has_value());
        BOOST_CHECK_NE(error->find(field), std::string::npos);
    };
    expectField([](ExecutionPayload& p) { p.extraData = bytes{0xff}; }, "extraData");
    expectField([](ExecutionPayload& p) { p.parentHash = h256(99); }, "parentHash");
    expectField([](ExecutionPayload& p) { p.stateRoot = h256(99); }, "stateRoot");
    expectField([](ExecutionPayload& p) { p.receiptsRoot = h256(99); }, "receiptsRoot");
    expectField([](ExecutionPayload& p) { p.logsBloom.fill(0x22); }, "logsBloom");
    expectField([](ExecutionPayload& p) { p.prevRandao = h256(99); }, "prevRandao");
    expectField([](ExecutionPayload& p) { p.gasLimit = 99; }, "gasLimit");
    expectField([](ExecutionPayload& p) { p.gasUsed = 99; }, "gasUsed");
    expectField([](ExecutionPayload& p) { p.baseFeePerGas = 99; }, "baseFeePerGas");
    expectField([](ExecutionPayload& p) { p.blockHash = h256(99); }, "blockHash");
    expectField([](ExecutionPayload& p) { p.feeRecipient = Address(99); }, "feeRecipient");
    expectField([](ExecutionPayload& p) { p.timestamp = 99; }, "timestamp");
    expectField([](ExecutionPayload& p) { p.blockNumber = 99; }, "blockNumber");
    expectField([](ExecutionPayload& p) { p.transactions[0].raw = bytes{0xbb}; }, "transactions");
    expectField([](ExecutionPayload& p) { p.withdrawalsRoot = h256(99); }, "withdrawalsRoot");
    expectField([](ExecutionPayload& p) { p.blobGasUsed = u256(99); }, "blobGasUsed");
    expectField([](ExecutionPayload& p) { p.excessBlobGas = u256(99); }, "excessBlobGas");
}

// The withdrawals LIST is the field the root commits to: with both roots absent
// (the V2/V3 wire shape) or the submitted root copied from the build, a tampered
// list under the same blockHash must still be rejected — an honest echo always
// carries the built list.
BOOST_AUTO_TEST_CASE(compare_with_built_payload_rejects_tampered_withdrawals_list)
{
    auto base = []() {
        ExecutionPayload payload;
        payload.extraData = bytes{0x01};
        payload.parentHash = h256(1);
        payload.stateRoot = h256(2);
        payload.receiptsRoot = h256(3);
        payload.prevRandao = h256(4);
        payload.gasLimit = 5;
        payload.gasUsed = 6;
        payload.baseFeePerGas = 7;
        payload.blockHash = h256(8);
        payload.feeRecipient = Address(9);
        payload.timestamp = 10;
        payload.blockNumber = 11;
        payload.withdrawals = std::vector<WithdrawalV1>{};
        return payload;
    };
    auto const built = base();
    BOOST_CHECK(!bcos::engine::detail::compareWithBuiltPayload(built, built).has_value());

    auto submitted = base();
    submitted.withdrawals = std::vector<WithdrawalV1>{WithdrawalV1{}};
    auto error = bcos::engine::detail::compareWithBuiltPayload(submitted, built);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_NE(error->find("withdrawals"), std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
