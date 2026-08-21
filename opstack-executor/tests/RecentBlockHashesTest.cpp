// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// RecentBlockHashesTest — runtime coverage for the lazy-loading BlockHashes (op-geth GetHashFn
// semantics): the range guards, the {N-1: parentHash} seed, the SYS_NUMBER_2_HASH lookup, the
// value-length poison channel, and the cache short-circuit on repeat queries.

#include <opstack-executor/RecentBlockHashes.h>

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using Hashes = bcos::evm::engine::detail::RecentBlockHashes<MutableStorage>;

constexpr int64_t kBlockNumber = 100;

evmc::bytes32 filledBytes32(uint8_t fill)
{
    evmc::bytes32 h{};
    std::memset(h.bytes, fill, sizeof(h.bytes));
    return h;
}

bool isZero(const evmc::bytes32& h)
{
    static constexpr evmc::bytes32 kZero{};
    return std::memcmp(h.bytes, kZero.bytes, sizeof(h.bytes)) == 0;
}

/// Write a raw value row into SYS_NUMBER_2_HASH (the table RecentBlockHashes reads).
void writeHashRow(MutableStorage& storage, int64_t n, std::string value)
{
    bcos::storage::Entry e;
    e.set(std::move(value));
    bcos::task::syncWait(bcos::storage2::writeOne(
        storage, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(n)}, std::move(e)));
}

/// A 32-byte row whose content is `fill` repeated — a valid ancestor hash.
void writeValidHashRow(MutableStorage& storage, int64_t n, uint8_t fill)
{
    writeHashRow(storage, n, std::string(sizeof(evmc::bytes32::bytes), static_cast<char>(fill)));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RecentBlockHashesTest)

BOOST_AUTO_TEST_CASE(OutOfRangeQueriesReturnZero)
{
    MutableStorage storage;
    std::optional<std::string> hashErr;
    Hashes hashes(storage, kBlockNumber, filledBytes32(0xaa), &hashErr);

    BOOST_CHECK(isZero(hashes.get_block_hash(kBlockNumber)));      // n == blockNumber
    BOOST_CHECK(isZero(hashes.get_block_hash(kBlockNumber + 5)));  // n > blockNumber
    BOOST_CHECK(isZero(hashes.get_block_hash(-1)));                // n < 0
    BOOST_CHECK(!hashErr.has_value());
}

BOOST_AUTO_TEST_CASE(SeededParentHashHitsWithoutStorage)
{
    MutableStorage storage;  // deliberately empty — the seed must not touch storage
    std::optional<std::string> hashErr;
    const auto parentHash = filledBytes32(0xaa);
    Hashes hashes(storage, kBlockNumber, parentHash, &hashErr);

    const auto got = hashes.get_block_hash(kBlockNumber - 1);
    BOOST_CHECK(std::memcmp(got.bytes, parentHash.bytes, sizeof(got.bytes)) == 0);
    BOOST_CHECK(!hashErr.has_value());
}

BOOST_AUTO_TEST_CASE(MissingRowReturnsZeroWithoutError)
{
    MutableStorage storage;
    std::optional<std::string> hashErr;
    Hashes hashes(storage, kBlockNumber, filledBytes32(0xaa), &hashErr);

    // n = 50 is below the seeded N-1, so the lookup reaches SYS_NUMBER_2_HASH and misses.
    BOOST_CHECK(isZero(hashes.get_block_hash(50)));
    BOOST_CHECK(!hashErr.has_value());  // a pruned/missing row is NOT a poison event
}

BOOST_AUTO_TEST_CASE(BadLengthRowPoisonsErrorChannel)
{
    MutableStorage storage;
    writeHashRow(storage, 50, std::string("abcd"));  // 4 bytes, not a 32-byte hash
    std::optional<std::string> hashErr;
    Hashes hashes(storage, kBlockNumber, filledBytes32(0xaa), &hashErr);

    BOOST_CHECK(isZero(hashes.get_block_hash(50)));
    BOOST_REQUIRE(hashErr.has_value());
    BOOST_CHECK(hashErr->find("length != 32") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ValidRowReturnsStoredHash)
{
    MutableStorage storage;
    writeValidHashRow(storage, 50, 0xbc);
    std::optional<std::string> hashErr;
    Hashes hashes(storage, kBlockNumber, filledBytes32(0xaa), &hashErr);

    const auto got = hashes.get_block_hash(50);
    const auto expected = filledBytes32(0xbc);
    BOOST_CHECK(std::memcmp(got.bytes, expected.bytes, sizeof(got.bytes)) == 0);
    BOOST_CHECK(!hashErr.has_value());
}

BOOST_AUTO_TEST_CASE(SecondQueryHitsCacheWithoutStorageRead)
{
    MutableStorage storage;
    writeValidHashRow(storage, 50, 0xbc);
    std::optional<std::string> hashErr;
    Hashes hashes(storage, kBlockNumber, filledBytes32(0xaa), &hashErr);

    const auto first = hashes.get_block_hash(50);  // storage read -> cached
    BOOST_CHECK(!isZero(first));

    // Corrupt the row after the cache fill: a second storage read would poison the error
    // channel and return zero. A cache hit must return the cached value untouched.
    writeHashRow(storage, 50, std::string("abcd"));
    const auto second = hashes.get_block_hash(50);
    BOOST_CHECK(std::memcmp(second.bytes, first.bytes, sizeof(second.bytes)) == 0);
    BOOST_CHECK(!hashErr.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
