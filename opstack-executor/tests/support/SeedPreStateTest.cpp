// opstack-executor/tests/support/SeedPreStateTest.cpp
#include "SeedPreState.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <sstream>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

[[maybe_unused]] Json::Value loadJson(std::string const& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ss.str(), root))
    {
        throw std::runtime_error("bad json " + path);
    }
    return root;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SeedPreStateSuite)

BOOST_AUTO_TEST_CASE(SeedAccountsAndVerify)
{
    // A minimal pre: 2 accounts (a contract account with a storage slot + a plain EOA)
    Json::Value pre(Json::objectValue);
    // 0x4200000000000000000000000000000000000015 — L1 block contract with 1 storage slot
    pre["0x4200000000000000000000000000000000000015"] = Json::objectValue;
    pre["0x4200000000000000000000000000000000000015"]["balance"] = "0x0";
    pre["0x4200000000000000000000000000000000000015"]["nonce"] = "0x1";
    pre["0x4200000000000000000000000000000000000015"]["code"] = "0x";
    // Warning: storage values must be full 32 bytes (66 hex) — jsonBytes32 throws
    // runtime_error on short values.
    pre["0x4200000000000000000000000000000000000015"]["storage"]
       ["0x0000000000000000000000000000000000000000000000000000000000000001"] =
           "0x0000000000000000000000000000000000000000000000000000000000001234";
    // 0x7e5f... — a normal EOA with a balance
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["balance"] = "0x56bc75e2d63100000";
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["nonce"] = "0x0";
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["code"] = "0x";

    // buckets=1: a multi-bucket CONCURRENT backend iterates range() bucket-by-bucket (not
    // globally ordered), which breaks probeHasStorage's table-contiguity early-exit. The
    // CONCURRENT flag must stay: MemoryStorage's cross-type merge() requires it on the target.
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);

    opstack_test::seedPreState(multiLayerStorage, pre);

    // Verify: fork a new view and read back through EVMAccount (the same access path the
    // executors use).
    auto view = multiLayerStorage.fork();
    const auto addr = opstack_test::jsonAddress("0x7e5f4552091a69125d5dfcb7b8c2659029395bdf");
    auto acct = bcos::executor_v1::eth::ethViewAccount(view, addr);
    BOOST_REQUIRE(bcos::task::syncWait(acct.exists()));
    BOOST_CHECK(bcos::task::syncWait(acct.balance()) ==
                opstack_test::jsonU256("0x56bc75e2d63100000"));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(acct.nonce()).value_or(""), "0");
    const auto l1 = opstack_test::jsonAddress("0x4200000000000000000000000000000000000015");
    auto l1Acct = bcos::executor_v1::eth::ethViewAccount(view, l1);
    BOOST_REQUIRE(bcos::task::syncWait(l1Acct.exists()));
    // Positive anchors: a seeding no-op would still pass the zero-valued checks above —
    // pin the seeded nonce / storage-presence / empty code explicitly.
    BOOST_CHECK_EQUAL(bcos::task::syncWait(l1Acct.nonce()).value_or(""), "1");
    const auto slot = opstack_test::jsonBytes32(
        "0x0000000000000000000000000000000000000000000000000000000000000001");
    BOOST_CHECK(bcos::task::syncWait(l1Acct.storage(slot)) ==
                opstack_test::jsonBytes32(
                    "0x0000000000000000000000000000000000000000000000000000000000001234"));
    // The EOA has no storage rows: any slot reads back as zero.
    BOOST_CHECK(bcos::task::syncWait(acct.storage(slot)) == evmc::bytes32{});
    auto const l1Code = bcos::task::syncWait(l1Acct.code());
    BOOST_CHECK(!l1Code.has_value() || l1Code->get().empty());
    auto const code = bcos::task::syncWait(acct.code());
    BOOST_CHECK(!code.has_value() || code->get().empty());
}

BOOST_AUTO_TEST_CASE(RejectsOddLengthHex)
{
    // bcos::fromHex would left-pad these to valid-length but wrong values.
    BOOST_CHECK_THROW(opstack_test::jsonAddress("0x123"), std::runtime_error);
    BOOST_CHECK_THROW(opstack_test::jsonBytes32("0xabc"), std::runtime_error);
    BOOST_CHECK_THROW(opstack_test::jsonBytes("0x1"), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
