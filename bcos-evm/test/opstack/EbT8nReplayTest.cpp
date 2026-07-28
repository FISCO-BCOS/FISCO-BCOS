// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EbT8nReplayTest.cpp — 真账本桥 Task 6：E-b gate（design §2/§7）。33 向量经真桥
// （Storage2Ledger over 内存版 storage2）回放，与既有 TestState 腿
// （OpT8nReplayTest.cpp）、MemoryLedger 腿（MemoryLedgerT8nReplayTest.cpp）共用同一份
// T8nReplayHarness.h::replayAllVectors<Backend> 模板、同一份 test/opstack/t8n/
// vectors/*.json + DIVERGENCES.md，仅播种/写回/建根/postState 遍历的后端换成
// Storage2Ledger<CountingStorage<MultiLayerStorage 的 view>>。
//
// fixture（design §2"E-b 判据环境" + brief"仿 testMultiLayerStorage.cpp:20-37"）：
// 每向量新建一份独立 MultiLayerStorage（ORDERED|LOGICAL_DELETION 可变层 +
// ORDERED|CONCURRENT 后端，零 IO，ctest 常驻），fork()→newMutable() 取一枚可写 view，
// CountingStorage 包一层做接线完整性计数（design §7 探针 5），最终交给 Storage2Ledger。
// 33 向量互不共享底层存储——与"一块一实例"的桥生命周期契约（design §4.2）一致，也
// 与另两腿（各自 fromPre 独立构造账本）同构。
//
// 本文件仅在 in-tree 构建（bcos-framework 目标存在）编译，见 test/CMakeLists.txt 的
// if(TARGET bcos-framework) 条件块——standalone 构建不含 bcos-framework 传递依赖。

#include "T8nReplayHarness.h"

#include <bcos-evm/ledger/LedgerSeed.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "support/CountingStorage.h"

namespace
{

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

// 最小 CheckpointStorage 桩——仅 MultiLayerStorage 需要的接口（open()/历史 checkpoint
// 不支持/createCheckpoint·deleteCheckpoint 为 no-op）。每个测试模块各自留一份极小拷贝，
// 而非跨模块引用 transaction-scheduler/tests/TrivialCheckpointStorage.h（该目录是另一
// 模块的测试私有代码；本仓已有先例：engine/test/unittests/engine/EngineServiceTest.cpp:84-98
// 同样本地复刻一份，而非跨目录 include）。
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // E-b 判据环境为单层内存 fixture，不需要历史 checkpoint（design §2）。
    }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

// Storage2Backend（真账本桥 Task 6，E-b gate 主角）。
//
// Ledger = unique_ptr<Impl> 而非直接返回 Impl 值：Impl 内 bridge（Storage2Ledger）持有
// 对同一 Impl 内 counting 成员的引用（自引用结构）——Storage2Ledger 显式删除了拷贝/移动
// 构造（design §4.1"桥实例单线程使用"契约的自然推论），含 Storage2Ledger 成员的 Impl
// 因而也隐式不可移动。unique_ptr 给 Impl 一个稳定堆地址：fromPre 按值返回 Ledger 时只
// 搬移指针本身，Impl 内部各成员地址不变，自引用不失真。
struct Storage2Backend
{
    struct Impl
    {
        using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
            memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
        using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
            memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
            std::hash<StateKey>>;
        using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
        using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
        using ViewType = typename MLS::ViewType;
        using Counting = bcos::evm::test::CountingStorage<ViewType>;
        using Bridge = bcos::evm::ledger::Storage2Ledger<Counting>;

        BackendMemStorage backendStorage;
        CheckpointBackend checkpointBackend;
        MLS multiLayerStorage;
        ViewType view;
        Counting counting;
        Bridge bridge;

        Impl()
          : checkpointBackend(backendStorage),
            multiLayerStorage(checkpointBackend),
            view(multiLayerStorage.fork()),
            counting(view),
            bridge(counting)
        {
            view.newMutable();
        }
    };

    using Ledger = std::unique_ptr<Impl>;

    static Ledger fromPre(const evmone::test::TestState& pre)
    {
        auto impl = std::make_unique<Impl>();
        bcos::evm::ledger::seedFromTestState(impl->bridge, pre);
        return impl;
    }

    static const evmone::state::StateView& view(Ledger& ledger) { return ledger->bridge; }

    static void apply(Ledger& ledger, const evmone::state::StateDiff& diff)
    {
        ledger->bridge.applyDiff(diff);
    }

    static evmone::hash256 stateRoot(const Ledger& ledger)
    {
        return bcos::evm::stateRootOf(ledger->bridge);
    }

    static void forEachPostAccount(const Ledger& ledger, const PostVisitor& visitor)
    {
        ledger->bridge.visitAccounts([&](const auto& av) {
            visitor(av.addr, av.nonce, av.balance, av.code(), av.storage);
            return true;
        });
    }

    // 向量末钩子（design §4.3 毒旗消费方契约 + §7 探针 5 接线完整性）：块执行结束必须
    // 检查 poisoned()；storage2 实际读写调用计数经 RecordProperty 留痕，reads>0 是"gate
    // 真实依赖 storage2 路径而非假后端"的常驻机器判据。
    static void afterVector(const std::string& id, const Ledger& ledger)
    {
        EXPECT_FALSE(ledger->bridge.poisoned())
            << id << ": Storage2Ledger poisoned: " << ledger->bridge.firstError();

        const auto reads = ledger->counting.readCount;
        const auto writes = ledger->counting.writeCount;
        ::testing::Test::RecordProperty("storage2_reads", static_cast<int>(reads));
        ::testing::Test::RecordProperty("storage2_writes", static_cast<int>(writes));
        ASSERT_GT(reads, 0U) << id
                             << ": zero storage2 reads observed — Storage2Ledger not wired "
                                "to the t8n replay (wiring-integrity probe, design §7 #5)";
    }
};

}  // namespace

TEST(EbT8nReplay, Vectors)
{
    replayAllVectors<Storage2Backend>("storage2");
}
