// bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp
// W6 L2 端到端真链对拍：真实 JSON params → EngineHelper::parseNewPayloadRequest(V4)
// → EngineService<OpSchedulerImpl>.newPayload(4) → executeOpBlock → 七项断言 vs golden。
// fixture 组合仿 val-loop EngineNewPayloadGateTest 的 GateFixture（member 顺序
// storage→memPool→executor→receiptFactory→scheduler→blockFactory→service）。
#include "support/GoldenSample.h"
#include "support/SeedPreState.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/engine/OpEngineSeam.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
// EngineHelper.h 的 parseNewPayloadRequest 声明引用 bcos::protocol::TransactionFactory&，
// 但 EngineHelper.h 自身不声明该类型（生产靠 bcos-rpc unity-build 的 include 顺序）。
// 单 TU 直编必须先把 TransactionFactory.h 引入，否则声明处即报错。
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-task/Wait.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <json/json.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{

// ── storage fixture（Task 2 测试同款）──
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
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }
};
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT), std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

// ── 组合根 stand-ins（val-loop GateFixture 同款：OP 模式不经 memPool/executor）──
struct StubMemPool {};
struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
    };
    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }
    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory = std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

constexpr uint64_t kChainId = 0x2105;

bcos::evm::opstack::OpForkTimestamps forkTimestampsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = 0,
        .jovianTime = jovian ? 0 : std::numeric_limits<uint64_t>::max(),
    };
}

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, OpScheduler>;

struct OpE2eFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    OpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpEngineService service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : scheduler(receiptFactory, kChainId, forkTimestamps),
        service(memPool, multiLayerStorage, executor, scheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4)
    {}
};

/// 产出的 header：生产映射重构（val-loop GateFixture 的 productionHeaderOf 模式）。
/// `bcos::engine::detail::rebuildOpEthHeader`（EngineServiceImpl.cpp:470 附近：17 字段来自
/// payload 逐字 + txRoot + 3 常量）；OP block hash 用 `opHeaderHash(c)` =
/// keccak256(encodeOpHeader())，不得用 BlockHeader::hash()（dataHash 空/工厂 TARS 序回填）。
bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = OpScheduler::computeTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(blockFactory->blockHeaderFactory(), payload,
        transactionsRoot, *request.parentBeaconBlockRoot);
}

/// Parent 预登记（R3/R5 致命缺口 A 修复）：OP 路径 step-3 parentKnown（EngineServiceImpl.h:821-827）
/// 查 SYS_HASH_2_NUMBER 判 parent-known。33 个孤立向量都是 block 1，parent 必须以「受信创世」
/// 预登记，否则 newPayload 返回 SYNCING。写入编码必须是生产同款：key=hash 原始 32 字节，
/// value=number 十进制字符串（gate 测试 registerVerifiedBlock，EngineNewPayloadGateTest.cpp:188-198）。
void registerVerifiedBlock(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    multiLayerStorage.pushView(std::move(view));
}

// ── 七项断言 ──
void assertSevenFields(std::string const& id,
    bcos::protocol::BlockHeader::Ptr const& produced,
    bcostars::protocol::BlockHeaderImpl::Ptr const& goldenHeader,
    bcos::h256 const& goldenBlockHash)
{
    const auto c = bcos::engine::detail::opHeaderConst();
    // 1. blockHash：produced opHeaderHash = keccak256(encodeOpHeader())，须等于 golden.blockHash
    //    （op-geth 的 block.Hash() = keccak(RLP(21 字段)) 定义）。
    // ⚠️ 本仓库 Boost.Test 宏不支持 `<< id << msg` 链式；统一 BOOST_CHECK_MESSAGE(id << msg)。
    BOOST_CHECK_MESSAGE(produced->opHeaderHash(c) == goldenBlockHash, id << ": blockHash");
    BOOST_CHECK_MESSAGE(produced->stateRoot() == goldenHeader->stateRoot(), id << ": stateRoot");
    BOOST_CHECK_MESSAGE(
        produced->receiptsRoot() == goldenHeader->receiptsRoot(), id << ": receiptsRoot");
    BOOST_CHECK_MESSAGE(produced->withdrawalsRoot() == goldenHeader->withdrawalsRoot(),
        id << ": withdrawalsRoot");
    BOOST_CHECK_MESSAGE(produced->gasUsed() == goldenHeader->gasUsed(), id << ": gasUsed");
    BOOST_CHECK_MESSAGE(produced->txsRoot() == goldenHeader->txsRoot(), id << ": txRoot");
    // ⚠️ bytesConstRef(RefDataContainer) 的 operator== 是「指针+长度」浅比较，不是内容比较
    // （RefDataContainer.h:84-87）。produced/golden 各自持有同一份 256 字节 bloom 于不同存储，
    // 指针必不等 → 必须用 std::equal 做逐字节内容比较（encodeOpHeader 字节级断言已覆盖内容，
    // 这里单独保留七字段之一的位置）。
    BOOST_CHECK_MESSAGE(std::equal(produced->logsBloom().begin(), produced->logsBloom().end(),
                            goldenHeader->logsBloom().begin(), goldenHeader->logsBloom().end()),
        id << ": logsBloom");
    // 主断言：encodeOpHeader 字节级全等（覆盖全部字段的 RLP 编码）
    BOOST_CHECK_MESSAGE(produced->encodeOpHeader(c) == goldenHeader->encodeOpHeader(c),
        id << ": encodeOpHeader");
}

/// 一个向量端到端：seed pre → register parent → makeParamsJson → parseNewPayloadRequest(V4)
/// → newPayload(4) → 断言。
void runGoldenVector(std::string const& id)
{
    auto sample = w6test::loadVectorSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
    w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    // ⚠️ parent 预登记（缺口 A）：不登记 → SYNCING 而非 VALID。parentHash 从 golden header 解码
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(
        params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);

    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    // ⚠️ PayloadValidationStatus 是 enum class，无 operator<<；必须 static_cast<int> 比较
    // （全代码库既有 engine 测试同款，EngineServiceTest.cpp:312 等）。
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        id << ": expected VALID, got " << static_cast<int>(status.status)
           << (status.validationError ? " : " + *status.validationError : ""));

    // produced header（生产映射重构）+ golden header（复用上面 parent 预登记时已解码的 goldenHeader，
    // 勿重复声明——同一函数块重定义 goldenHeader 是编译错误，R2-A 捕获）
    auto produced = productionHeaderOf(fixture->blockFactory, request);
    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    assertSevenFields(id, produced, goldenHeader, goldenBlockHash);

    // 方案 B 写侧：OP 交易落 SYS_HASH_2_TX（tars 编码），s_eth_hash_2_rawtx 不再写。
    // newPayload 的 registerOpBlock（EngineServiceImpl.h:1160-1308）把每笔 raw EIP-2718 信封转
    // tars Transaction 写 SYS_HASH_2_TX[txHash]（extraTransactionHash=txHash 锁 D4），原始信封
    // 不再写 s_eth_hash_2_rawtx（D1）；0x04 (EIP-7702) 无 TransactionType 对应 → opEnvelopeToTars
    // 返回 nullopt → 表 absent，但 SYS_HASH_2_RECEIPT 仍写（D7）。
    auto const& rawTxs = *request.executionPayload.rawTransactions;
    auto& hashImpl = *fixture->blockFactory->cryptoSuite()->hashImpl();
    auto view = fixture->multiLayerStorage.fork();
    for (std::size_t i = 0; i < rawTxs.size(); ++i)
    {
        auto txHash = hashImpl.hash(rawTxs[i]);
        // 0x04 (EIP-7702)：opEnvelopeToTars 返回 nullopt → SYS_HASH_2_TX absent（D7）
        if (rawTxs[i].size() >= 1 && rawTxs[i][0] == 0x04)
        {
            auto zeroEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
                bcos::executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                    bcos::concepts::bytebuffer::toView(txHash)}));
            BOOST_CHECK_MESSAGE(
                !zeroEntry.has_value(), id << ": 0x04 tx #" << i << " SYS_HASH_2_TX absent");
            // SYS_HASH_2_RECEIPT 仍写（EngineServiceImpl.h:1287-1290）
            auto rcpEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
                bcos::executor_v1::StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                    bcos::concepts::bytebuffer::toView(txHash)}));
            BOOST_CHECK_MESSAGE(
                rcpEntry.has_value(), id << ": 0x04 tx #" << i << " SYS_HASH_2_RECEIPT present");
            continue;
        }
        // SYS_HASH_2_TX present + round-trip（tars 解码回 Transaction，hash==txHash 锁 D4）
        auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            bcos::executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(txEntry.has_value(), id << ": tx #" << i << " SYS_HASH_2_TX present");
        auto txBytes = bcos::bytesConstRef(
            reinterpret_cast<bcos::byte const*>(txEntry->get().data()), txEntry->get().size());
        auto tx = fixture->blockFactory->transactionFactory()->createTransaction(
            txBytes, /*checkSig=*/false, /*checkHash=*/false, /*tainted=*/false);
        BOOST_CHECK_MESSAGE(tx->hash() == txHash, id << ": tx #" << i << " round-trip hash==txHash");
        // rawtx 表 absent（D1：不保留）
        auto rawEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            bcos::executor_v1::StateKey{OpScheduler::c_ethRawTxTable,
                bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_CHECK_MESSAGE(
            !rawEntry.has_value(), id << ": tx #" << i << " s_eth_hash_2_rawtx absent");
    }
}

/// 链式双块（chainA/B，R2-C 核实流程）：只播 A 的 pre → 登记 A 的 parent(0) →
/// 先投 B(SYNCING) → 投 A(VALID) → 再投 B(VALID)。FCU 刻意省略（见实现提示 #4）。
void runChainedPair(std::string const& aId, std::string const& bId)
{
    auto sampleA = w6test::loadChainedSample(aId);
    auto sampleB = w6test::loadChainedSample(bId);
    BOOST_REQUIRE(sampleA.jovian == sampleB.jovian);  // 链式双块 fork 一致（isthmus 或 jovian）
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sampleA.jovian));

    // 只播 A 的 pre（B 的 pre 即 A 的 postState，绝不重播）
    w6test::seedPreState(fixture->multiLayerStorage, sampleA.vector["pre"]);
    const auto goldenHeaderA = w6test::decodeGoldenHeader(sampleA);
    // 登记 A 的 parent（受信创世 height 0）
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeaderA->parentInfo().blockHash, 0);

    auto requestA = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sampleA),
        *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto requestB = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sampleB),
        *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);

    // 先投 B：parent(A) 未登记 → SYNCING
    auto earlyB = bcos::task::syncWait(fixture->service.newPayload(requestB, 4));
    BOOST_CHECK_MESSAGE(static_cast<int>(earlyB.status) ==
            static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing),
        bId << ": first B should be SYNCING (parent A unknown)");

    // 投 A：VALID（registerOpBlock 写 SYS_HASH_2_NUMBER[hashA]=1）
    auto statusA = bcos::task::syncWait(fixture->service.newPayload(requestA, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusA.status) ==
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        aId << ": A expected VALID, got " << static_cast<int>(statusA.status));

    // 再投 B：parentKnown 命中 A → VALID
    auto statusB = bcos::task::syncWait(fixture->service.newPayload(requestB, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusB.status) ==
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        bId << ": B expected VALID after A, got " << static_cast<int>(statusB.status));

    // 各自七项断言（productionHeaderOf 从 request 重构，独立于执行）
    auto producedA = productionHeaderOf(fixture->blockFactory, requestA);
    const auto goldenBlockHashA = bcos::h256(std::string(sampleA.golden["blockHash"].asString()));
    assertSevenFields(aId, producedA, goldenHeaderA, goldenBlockHashA);
    auto producedB = productionHeaderOf(fixture->blockFactory, requestB);
    auto goldenHeaderB = w6test::decodeGoldenHeader(sampleB);
    const auto goldenBlockHashB = bcos::h256(std::string(sampleB.golden["blockHash"].asString()));
    assertSevenFields(bId, producedB, goldenHeaderB, goldenBlockHashB);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpNewPayloadRpcE2eSuite)

BOOST_AUTO_TEST_CASE(JovianDepositOnly) { runGoldenVector("jovian_deposit_only"); }

BOOST_AUTO_TEST_CASE(JovianTransferMulti) { runGoldenVector("jovian_transfer_multi"); }

BOOST_AUTO_TEST_CASE(JovianDaMix) { runGoldenVector("jovian_da_mix"); }

BOOST_AUTO_TEST_CASE(JovianFirstBlock) { runGoldenVector("jovian_first_block"); }

BOOST_AUTO_TEST_CASE(IsthmusDepositOnly) { runGoldenVector("isthmus_deposit_only"); }

BOOST_AUTO_TEST_CASE(IsthmusTransferMulti) { runGoldenVector("isthmus_transfer_multi"); }

BOOST_AUTO_TEST_CASE(IsthmusSetcode7702) { runGoldenVector("isthmus_setcode_7702"); }

BOOST_AUTO_TEST_CASE(IsthmusTxReverted) { runGoldenVector("isthmus_tx_reverted"); }

BOOST_AUTO_TEST_CASE(IsthmusBigBlock130tx) { runGoldenVector("isthmus_big_block_130tx"); }

// B-7：单 fork isthmus 的 system-call 顺序可观测向量（W5 审查 A#1 定 id）。顺序错 →
// L1 读者 REVERT → stateRoot 失配 → VALID 断言+七项断言变红。
BOOST_AUTO_TEST_CASE(SystemCallOrderObservable)
{
    runGoldenVector("isthmus_system_call_order_observable");
}

// ── 全部 33 向量（16 isthmus + 17 jovian），每个一行 ──
BOOST_AUTO_TEST_CASE(IsthmusAccessList) { runGoldenVector("isthmus_access_list"); }
BOOST_AUTO_TEST_CASE(IsthmusContractCreate) { runGoldenVector("isthmus_contract_create"); }
BOOST_AUTO_TEST_CASE(IsthmusContractLogs) { runGoldenVector("isthmus_contract_logs"); }
BOOST_AUTO_TEST_CASE(IsthmusDepositFailed) { runGoldenVector("isthmus_deposit_failed"); }
BOOST_AUTO_TEST_CASE(IsthmusDepositMint) { runGoldenVector("isthmus_deposit_mint"); }
BOOST_AUTO_TEST_CASE(IsthmusEmptyAccountCleanup) { runGoldenVector("isthmus_empty_account_cleanup"); }
BOOST_AUTO_TEST_CASE(IsthmusFeeEnvObserver) { runGoldenVector("isthmus_fee_env_observer"); }
BOOST_AUTO_TEST_CASE(IsthmusMessagePasserWrite) { runGoldenVector("isthmus_message_passer_write"); }
BOOST_AUTO_TEST_CASE(IsthmusSetcode7702Skips) { runGoldenVector("isthmus_setcode_7702_skips"); }
BOOST_AUTO_TEST_CASE(IsthmusSystemContractsReal) { runGoldenVector("isthmus_system_contracts_real"); }
BOOST_AUTO_TEST_CASE(IsthmusTransferBasic) { runGoldenVector("isthmus_transfer_basic"); }
BOOST_AUTO_TEST_CASE(JovianAccessList) { runGoldenVector("jovian_access_list"); }
BOOST_AUTO_TEST_CASE(JovianContractCreate) { runGoldenVector("jovian_contract_create"); }
BOOST_AUTO_TEST_CASE(JovianContractLogs) { runGoldenVector("jovian_contract_logs"); }
BOOST_AUTO_TEST_CASE(JovianDepositFailed) { runGoldenVector("jovian_deposit_failed"); }
BOOST_AUTO_TEST_CASE(JovianDepositMint) { runGoldenVector("jovian_deposit_mint"); }
BOOST_AUTO_TEST_CASE(JovianEmptyAccountCleanup) { runGoldenVector("jovian_empty_account_cleanup"); }
BOOST_AUTO_TEST_CASE(JovianFeeEnvObserver) { runGoldenVector("jovian_fee_env_observer"); }
BOOST_AUTO_TEST_CASE(JovianMessagePasserWrite) { runGoldenVector("jovian_message_passer_write"); }
BOOST_AUTO_TEST_CASE(JovianSetcode7702) { runGoldenVector("jovian_setcode_7702"); }
BOOST_AUTO_TEST_CASE(JovianSetcode7702Skips) { runGoldenVector("jovian_setcode_7702_skips"); }
BOOST_AUTO_TEST_CASE(JovianSystemContractsReal) { runGoldenVector("jovian_system_contracts_real"); }
BOOST_AUTO_TEST_CASE(JovianTransferBasic) { runGoldenVector("jovian_transfer_basic"); }
BOOST_AUTO_TEST_CASE(JovianTxReverted) { runGoldenVector("jovian_tx_reverted"); }

// 注：前 9 个样例 case 已覆盖 9 个向量；上面补全的 24 个 case 覆盖剩余 24 个，合计 33。
// 全量清单（16 isthmus + 17 jovian）：
//   isthmus: access_list, big_block_130tx, contract_create, contract_logs, deposit_failed,
//            deposit_mint, deposit_only, empty_account_cleanup, fee_env_observer,
//            message_passer_write, setcode_7702, setcode_7702_skips, system_contracts_real,
//            transfer_basic, transfer_multi, tx_reverted
//   jovian:  access_list, contract_create, contract_logs, da_mix, deposit_failed, deposit_mint,
//            deposit_only, empty_account_cleanup, fee_env_observer, first_block,
//            message_passer_write, setcode_7702, setcode_7702_skips, system_contracts_real,
//            transfer_basic, transfer_multi, tx_reverted
// 样例 case（9）：JovianDepositOnly/JovianTransferMulti/JovianDaMix/JovianFirstBlock/
//   IsthmusDepositOnly/IsthmusTransferMulti/IsthmusSetcode7702/IsthmusTxReverted/IsthmusBigBlock130tx
// 补全 case（24）：其余全部。⚠️ 命名冲突注意：BOOST_AUTO_TEST_CASE 名不可重复——
// 补全段的 24 个 case 名已刻意避开前 9 个样例 case 名（R2-D 实测：与既有 107 个 case 名、
// 计划内 33 个 case 名均零冲突）。
// 链式双块：一个 case（runChainedPair）同流执行 chainA+chainB（先 B SYNCING → A VALID → B VALID）。
BOOST_AUTO_TEST_CASE(ChainedAB) { runChainedPair("chainA", "chainB"); }

// B-5c：jovian 链式对。runChainedPair 内断言 block2 VALID = step 3a-2 baseFee 一致性校验
// 通过即验证 max 分支（baseFee 按父块推导 + 上取整封顶）。
BOOST_AUTO_TEST_CASE(JovianChainedAB) { runChainedPair("jovianChainA", "jovianChainB"); }
// 最终校验：36 用例 = 34 向量（33 + isthmus_system_call_order_observable）+ 2 链式对
// （chainA/B + jovianChainA/B，覆盖 4 个链式样本）。

BOOST_AUTO_TEST_SUITE_END()
