// RecentBlockHashesTest.cpp — D1 单元测试 (spec 测试 #1/#4/#5/#7/#8/#10 的 get_block_hash 层)。
#include "support/CountingStorage.h"
#include "support/ThrowingStorage.h"
#include <bcos-evm/ledger/RecentBlockHashes.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Error.h>
#include <gtest/gtest.h>
#include <boost/lexical_cast.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cstring>

using namespace bcos::evm::engine::detail;
using namespace bcos::evm::test;
using namespace evmc::literals;

namespace
{
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

// ---- 文件内本地 fixture (复制 OpSchedulerImplTest.cpp:137-177 的形态; storage2 无共享
// CheckpointBackend 类型, 每文件一份 TrivialCheckpointStorage 是仓库惯例) ----
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

struct Fixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MLS multiLayerStorage;
    ViewType view;
    Fixture()
      : checkpointBackend(backendStorage),
        multiLayerStorage(checkpointBackend),
        view(multiLayerStorage.fork())
    {
        view.newMutable();
    }
};

void putHash(ViewType& view, int64_t h, uint8_t byte)
{
    bcos::storage::Entry e;
    std::string v(32, byte);
    e.set(std::move(v));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_HASH, boost::lexical_cast<std::string>(h)},
        std::move(e)));
}
}  // namespace

// spec 测试 #1: 窗口内祖先懒加载。
TEST(RecentBlockHashes, LazyLoadsAncestorFromTable)
{
    Fixture fixture;
    putHash(fixture.view, 5, 0xab);
    std::optional<std::string> hashErr;
    RecentBlockHashes<ViewType> hashes(fixture.view, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(5).bytes[31], 0xab);
    EXPECT_FALSE(hashErr.has_value());
}

// spec 测试 #3/#8: 出界 (n>=N) 与负高度 → 零。
TEST(RecentBlockHashes, OutOfRangeReturnsZero)
{
    Fixture fixture;
    std::optional<std::string> hashErr;
    RecentBlockHashes<ViewType> hashes(fixture.view, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(7), evmc::bytes32{});
    EXPECT_EQ(hashes.get_block_hash(100), evmc::bytes32{});
    EXPECT_EQ(hashes.get_block_hash(-1), evmc::bytes32{});
    EXPECT_FALSE(hashErr.has_value());
}

// spec 测试 #3: 缺失行 → 零 (op-geth 裁剪/不可达语义)。
TEST(RecentBlockHashes, MissingRowReturnsZero)
{
    Fixture fixture;
    std::optional<std::string> hashErr;
    RecentBlockHashes<ViewType> hashes(fixture.view, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(3), evmc::bytes32{});
    EXPECT_FALSE(hashErr.has_value());
}

// spec 测试 #10 (G3): 坏值长度 (非 32 字节) → 毒旗 + 零。
TEST(RecentBlockHashes, BadLengthValuePoisons)
{
    Fixture fixture;
    bcos::storage::Entry e;
    e.set(std::string(16, 0xcc));  // 16 字节, 非法
    bcos::task::syncWait(bcos::storage2::writeOne(
        fixture.view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "5"}, std::move(e)));
    std::optional<std::string> hashErr;
    RecentBlockHashes<ViewType> hashes(fixture.view, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(5), evmc::bytes32{});
    EXPECT_TRUE(hashErr.has_value());
}

// spec 测试 #5 (G1): storage 异常 → 毒旗 + 零 (noexcept 下不崩溃)。
TEST(RecentBlockHashes, StorageErrorPoisons)
{
    Fixture fixture;
    ThrowingStorage<ViewType> throwing(fixture.view);
    std::optional<std::string> hashErr;
    RecentBlockHashes<decltype(throwing)> hashes(
        throwing, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(5), evmc::bytes32{});
    EXPECT_TRUE(hashErr.has_value());
}

// spec 测试 #4: 同高度二次查询命中缓存, 不再 readOne (CountingStorage 计数判据)。
TEST(RecentBlockHashes, SecondQueryHitsCache)
{
    Fixture fixture;
    putHash(fixture.view, 5, 0xcd);
    CountingStorage<ViewType> counting(fixture.view);
    std::optional<std::string> hashErr;
    RecentBlockHashes<decltype(counting)> hashes(
        counting, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    const auto first = hashes.get_block_hash(5);
    const auto second = hashes.get_block_hash(5);
    EXPECT_EQ(first, second);
    EXPECT_EQ(second.bytes[31], 0xcd);
    EXPECT_EQ(counting.readCount, 1u) << "second query must hit cache, not re-read the table";
    EXPECT_FALSE(hashErr.has_value());
}

// spec 测试 #7 (审查补充): 多不同高度并存 (N-2 与 N-5)。
TEST(RecentBlockHashes, MultipleDistinctAncestors)
{
    Fixture fixture;
    putHash(fixture.view, 2, 0xaa);
    putHash(fixture.view, 5, 0xbb);
    std::optional<std::string> hashErr;
    RecentBlockHashes<ViewType> hashes(fixture.view, /*blockNumber=*/7, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(5).bytes[31], 0xbb);
    EXPECT_EQ(hashes.get_block_hash(2).bytes[31], 0xaa);
    EXPECT_FALSE(hashErr.has_value());
}

// spec 测试 #2/#6: genesis 高度 + 首块种子。
TEST(RecentBlockHashes, GenesisAndFirstBlockSeed)
{
    Fixture fixture;
    putHash(fixture.view, 0, 0x11);
    std::optional<std::string> hashErr;
    RecentBlockHashes<ViewType> hashes(fixture.view, /*blockNumber=*/5, 0x01_bytes32, &hashErr);
    EXPECT_EQ(hashes.get_block_hash(0).bytes[31], 0x11);  // genesis 种子

    // 首块 (N=1): 查询高度 0 = N-1, cache 命中, 不读表。
    CountingStorage<ViewType> counting(fixture.view);
    std::optional<std::string> hashErr2;
    RecentBlockHashes<decltype(counting)> first(
        counting, /*blockNumber=*/1, 0x22_bytes32, &hashErr2);
    EXPECT_EQ(first.get_block_hash(0), 0x22_bytes32);
    EXPECT_EQ(counting.readCount, 0u) << "N-1 seed must be served from cache, zero storage reads";
    EXPECT_FALSE(hashErr2.has_value());
}
