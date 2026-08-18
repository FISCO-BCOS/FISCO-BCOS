// bcos-evm/test/opstack/support/SeedPreStateTest.cpp
#include "SeedPreState.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
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
    // A minimal pre: 3 accounts (incl. a contract account with storage + a fully empty account)
    Json::Value pre(Json::objectValue);
    // 0x4200000000000000000000000000000000000015 — L1 block contract with 2 storage slots
    pre["0x4200000000000000000000000000000000000015"] = Json::objectValue;
    pre["0x4200000000000000000000000000000000000015"]["balance"] = "0x0";
    pre["0x4200000000000000000000000000000000000015"]["nonce"] = "0x1";
    pre["0x4200000000000000000000000000000000000015"]["code"] = "0x";
    // Warning: storage values must be full 32 bytes (66 hex) — jsonBytes32 throws
    // runtime_error on short values (caught by R2-B: real vectors are all 66 chars; this
    // test literal once used "0x1234", which made Step 5 go red).
    pre["0x4200000000000000000000000000000000000015"]["storage"]
       ["0x0000000000000000000000000000000000000000000000000000000000000001"] =
           "0x0000000000000000000000000000000000000000000000000000000000001234";
    // 0x7e5f... — a normal EOA with a balance
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["balance"] = "0x56bc75e2d63100000";
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["nonce"] = "0x0";
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["code"] = "0x";

    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);

    w6test::seedPreState(multiLayerStorage, pre);

    // Verify: fork a new view and read back through the Storage2State bridge
    auto view = multiLayerStorage.fork();
    bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
    const auto addr = w6test::jsonAddress("0x7e5f4552091a69125d5dfcb7b8c2659029395bdf");
    const auto acct = bridge.get_account(addr);
    BOOST_REQUIRE(acct.has_value());
    BOOST_CHECK(acct->balance == intx::from_string<intx::uint256>("0x56bc75e2d63100000"));
    BOOST_CHECK_EQUAL(acct->nonce, 0u);
    const auto l1 = w6test::jsonAddress("0x4200000000000000000000000000000000000015");
    const auto l1Acct = bridge.get_account(l1);
    BOOST_REQUIRE(l1Acct.has_value());
    const auto slot =
        w6test::jsonBytes32("0x0000000000000000000000000000000000000000000000000000000000000001");
    BOOST_CHECK(
        bridge.get_storage(l1, slot) ==
        w6test::jsonBytes32("0x0000000000000000000000000000000000000000000000000000000000001234"));
    BOOST_CHECK_MESSAGE(
        !bridge.poisoned(), "seeding poisoned: " << std::string(bridge.firstError()));
}

BOOST_AUTO_TEST_SUITE_END()
