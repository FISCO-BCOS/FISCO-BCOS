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
    // 一个最小 pre：3 个账户（含带 storage 的合约账户 + 完全空账户）
    Json::Value pre(Json::objectValue);
    // 0x4200000000000000000000000000000000000015 — L1 block 合约，带 2 个 storage 槽
    pre["0x4200000000000000000000000000000000000015"] = Json::objectValue;
    pre["0x4200000000000000000000000000000000000015"]["balance"] = "0x0";
    pre["0x4200000000000000000000000000000000000015"]["nonce"] = "0x1";
    pre["0x4200000000000000000000000000000000000015"]["code"] = "0x";
    // ⚠️ storage 值必须是满 32 字节（66 hex）——jsonBytes32 对短值抛 runtime_error
    // （R2-B 捕获：真实向量全部 66 字符，此处测试字面量曾用 "0x1234" 导致 Step 5 必红）。
    pre["0x4200000000000000000000000000000000000015"]["storage"]
       ["0x0000000000000000000000000000000000000000000000000000000000000001"] =
           "0x0000000000000000000000000000000000000000000000000000000000001234";
    // 0x7e5f... — 普通 EOA，带 balance
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["balance"] = "0x56bc75e2d63100000";
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["nonce"] = "0x0";
    pre["0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"]["code"] = "0x";

    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);

    w6test::seedPreState(multiLayerStorage, pre);

    // 验证：fork 新 view，经 Storage2Ledger 桥读回
    auto view = multiLayerStorage.fork();
    bcos::evm::ledger::Storage2Ledger<ViewType> bridge(view);
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
