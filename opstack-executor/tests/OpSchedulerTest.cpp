// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerTest — 接线 Task 4（OpScheduler 新类）的最小 OP 块 + 三分类单测。
//
// 1. ExecutesMinimalOpBlockEqualToDirectExecuteOpBlock：最小 OP 块（deposit + 1 normal 带
// envelope，
//    SEV-8：extraTransactionBytes=完整信封，先例 OpDualPathEquivalenceTest.cpp:566-568）经
//    OpScheduler.executeBlock 驱动 == 直调 executeOpBlock 结果（receipts/status/gasUsed +
//    六字段承诺）。 语料锚：isthmus_transfer_basic.json 的 deposit + eip1559 envelope（op-geth
//    真实签名）； 直调锚 = 同一 MLS 上独立 OpSchedulerImpl 的 executeOpBlock。
// 2. ConsensusRejectionClassifiedAsOpConsensusRejected：execute hook 抛 OpConsensusError
//    （unsupported tx type byte 0x03，decodeOneRawTx 确定性抛）→ 骨架 coExecuteBlock 经
//    classifyException → Error 码 == OpConsensusRejected。
// 3. classifyException 直调三分类：OpConsensusError→OpConsensusRejected / OpStorageError→
//    OpStorageFault / 其它→UnknownError。
// 4. CommitPersistsSevenLedgerTables（Task 5c 槽位 3 E2E）：executeBlock + commitBlock 后 7 张
//    SYS 表落盘断言（SEV-10，取代被删 OpBlockScheduler 的 RefuseStubs）。
//
// 黄金约束：最小 OP 块 receipts/status/gasUsed == 直调 executeOpBlock。
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpTxDecode.h>  // detail::canonicalEnvelopeBytes（deposit 信封重建）

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <engine/bcos-engine/EngineServiceImpl.h>  // detail::opEnvelopeToTars（测试 link engine）
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using evmc::literals::operator""_address;
using evmc::literals::operator""_bytes32;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
namespace detail = bcos::evm::engine::detail;

constexpr uint64_t kChainId = 0x2105;  // 8453 — the FISCO OP chain id (vector eip1559 chainId)
const bcos::Address kSender{"0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"};  // eip1559 recovered
                                                                            // sender

// 语料 isthmus_transfer_basic.json：block.transactions[1]._op_raw（op-geth 签名 eip1559 信封）。
constexpr const char* kEip1559EnvelopeHex =
    "0x02f874822105808405f5e100847735940082520894b0b0000000000000000000000000000000000001880de"
    "0b6b3a764000080c001a0e37533ddb9f696c0b21788f1b00c78adc4a81b1d811d84e70fad672096fc924ea00ae"
    "693f4d68955a4c01ee8bab26f5be740ee416dd2556822f68b747d5aab7714";

// 最小 CheckpointStorage stub（源分支 fixture 同款：不 cross-include 其它模块测试私有头）。
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // 该 fixture 永远不需要历史 checkpoint。
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

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

/// composition-root 注入的 EnvelopeToTarsConverter：包 engine 的 opEnvelopeToTars（测试 link
/// engine， 生产侧由 Initializer 以同形 lambda 注入——OpScheduler 不内建）。
bcos::evm::engine::EnvelopeToTarsConverter makeConverter()
{
    return [](bcos::bytes const& env, bcos::crypto::HashType const& txHash) {
        return bcos::engine::detail::opEnvelopeToTars(env, txHash);
    };
}

/// L1 attributes deposit（语料 isthmus_transfer_basic 的 deposit 结构）：to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR 满足 isL1AttributesTx（OpBlockExecute.h:97-99）。data
/// 用空——Isthmus（pre-Jovian） 下 validateJovianBlockShape 是
/// no-op（OpBlockExecute.h:76），processOpBlock 只按 content 判 isL1AttributesTx，不校验
/// calldata（先例 OpBlockInjectorTest.cpp:88-101 空 data 的 deposit 同样过）。
bcos::evm::opstack::DepositTx makeDeposit()
{
    bcos::evm::opstack::DepositTx dep;
    dep.source_hash = 0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7_bytes32;
    dep.from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
    dep.to = 0x4200000000000000000000000000000000000015_address;
    dep.mint = std::nullopt;
    dep.value = intx::uint256{0};
    dep.gas_limit = 0xf4240;  // 1000000（语料 gas: "0xf4240"）
    dep.is_system_tx = false;
    dep.data = {};
    return dep;
}

/// 语料环境（isthmus_transfer_basic env）的 OP 头。timestamp 存毫秒（FISCO 惯例，/1000 给 OP 秒）。
/// commitment 字段（stateRoot/txsRoot/receiptsRoot/gasUsed/withdrawalsRoot/logsBloom/requestsHash）
/// 由调用方在直调 executeOpBlock 后按结果回填——announced 头即真实承诺，verify 六字段对比通过。
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader()
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(0x3f2 * 1000);  // 0x3f2 = 1010 s（OP 秒）→ 1_010_000 ms
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0,
        .blockHash =
            bcos::h256{"0x45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74"}});
    h->setCoinbase(bcos::Address{"0x4200000000000000000000000000000000000011"});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(0x989680));  // 10000000（语料 currentGasLimit）
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(0x3a699d00));  // 981000000（语料 currentBaseFee）
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(
        bcos::h256{"0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// 用 executeOpBlock 结果回填 announced 头的承诺字段（finishExecute 也写同一批字段，verify
/// 因此相等）。
void fillAnnouncedHeader(bcos::protocol::BlockHeader::Ptr const& header,
    bcos::evm::engine::OpExecuteBlockResult const& result)
{
    header->setStateRoot(result.stateRoot);
    header->setTxsRoot(result.txRoot);
    header->setReceiptsRoot(detail::toBcosH256(result.seal.receiptsRoot));
    header->setGasUsed(bcos::u256(result.gasUsed));
    header->setLogsBloom(bcos::bytesConstRef(result.seal.logsBloom.bytes, 256));
    header->setWithdrawalsRoot(detail::toBcosH256(result.seal.withdrawalsRoot));
    if (result.seal.requestsHash.has_value())
        header->setRequestsHash(detail::toBcosH256(*result.seal.requestsHash));
    if (result.seal.blobGasUsed.has_value())
        header->setBlobGasUsed(bcos::u256(*result.seal.blobGasUsed));
}

/// opEnvelopeToTars + SEV-8 覆写完整信封（先例 OpDualPathEquivalenceTest.cpp:566-568）：
/// takeToTarsTransaction 存 signing preimage，executeTransaction 读 extraTransactionBytes 当
/// 完整信封（OpstackExecutor.h:280-281）。
bcos::protocol::Transaction::Ptr buildFiscoTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto txHash = hashImpl->hash(env);
    auto tarsTx = bcos::engine::detail::opEnvelopeToTars(env, txHash);
    if (!tarsTx)
    {
        return nullptr;
    }
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(*tarsTx)]() mutable { return &tars; });
    return tx;
}

/// 一个 0x03 字节的信封——decodeOneRawTx 确定性抛 OpConsensusError（"unsupported tx type byte"，
/// OpTxDecode.h:405）。用于把 execute hook 稳定驱动到 consensus-rejected 分类（不依赖 RTTI 边界
/// 的运行时行为）。
bcos::protocol::Transaction::Ptr buildUnsupportedTypeTx()
{
    bcostars::Transaction tars;
    tars.extraTransactionBytes.push_back(0x03);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    return tx;
}

/// 在 MLS 后端种入 eip1559 发送方账户（StorageStateView::exists() 需非零 codeHash——create() +
/// setCode(empty)，裸 setBalance 会判不存在）。
void seedSender(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// 测试子类：暴露最近一次 pending 执行结果的 receipts（骨架 m_results 是 protected，派生可读）。
class TestOpScheduler : public bcos::executor_v1::opstack::OpScheduler<MLS>
{
public:
    using bcos::executor_v1::opstack::OpScheduler<MLS>::OpScheduler;

    std::vector<bcos::protocol::TransactionReceipt::Ptr> lastExecutedReceipts()
    {
        std::unique_lock<std::mutex> lock(this->m_resultsMutex);
        if (this->m_results.empty())
            return {};
        return this->m_results.front()->receipts;
    }
};

struct Fixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::evm::opstack::OpForkTimestamps forkTimestamps{.isthmusTime = 0,
        .jovianTime = std::numeric_limits<uint64_t>::max()};
    std::shared_ptr<TestOpScheduler> scheduler;

    Fixture()
      : scheduler(std::make_shared<TestOpScheduler>(receiptFactory, hashImpl, kChainId,
            forkTimestamps, blockFactory, multiLayerStorage, makeConverter()))
    {
        seedSender(multiLayerStorage, kSender, hashImpl);
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSuite)

/// 黄金约束：最小 OP 块（deposit + 1 normal 带 envelope）经 OpScheduler == 直调 executeOpBlock。
BOOST_AUTO_TEST_CASE(ExecutesMinimalOpBlockEqualToDirectExecuteOpBlock)
{
    Fixture f;

    // 信封：deposit 用 canonicalEnvelopeBytes 重建（0x7e || rlp([...8 字段])，OpTxDecode.h:307）；
    // normal 用语料的真实 op-geth 签名 envelope。两者都经 decodeOneRawTx 的 canonical round-trip。
    auto depTx = makeDeposit();
    bcos::bytes depEnv = detail::canonicalEnvelopeBytes(bcos::evm::opstack::OpBlockTx{depTx, {}});
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, eipEnvBytes};

    auto header = makeHeader();

    // 直调锚：同一 MLS 上独立 OpSchedulerImpl（route A），同一 header 环境。
    bcos::evm::engine::OpSchedulerImpl<ViewType> directScheduler(
        f.receiptFactory, kChainId, f.forkTimestamps);
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA;
    try
    {
        resultA = bcos::task::syncWait(directScheduler.executeOpBlock(viewA, *header, rawTxBytes));
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR("direct executeOpBlock threw: " << e.what());
        return;
    }
    catch (...)
    {
        BOOST_ERROR("direct executeOpBlock threw an unknown (RTTI-bypassed) exception");
        return;
    }

    // 直调成功且两笔都执行。
    BOOST_REQUIRE_EQUAL(resultA.receipts.size(), rawTxBytes.size());
    BOOST_CHECK_GT(resultA.gasUsed, 0);

    // announced 头回填直调承诺（SYS_NUMBER_2_BLOCK_HEADER 未落——execute 路径不写头表，只对比）。
    fillAnnouncedHeader(header, resultA);

    // 块装配：extraTransactionBytes = 完整信封（SEV-8）。
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    // OpScheduler 驱动。
    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool sysBlock = false;
    bool called = false;
    f.scheduler->executeBlock(block, /*verify=*/true,
        [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool s) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
            sysBlock = s;
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        err == nullptr, "executeBlock failed: " << (err ? err->errorMessage() : ""));
    BOOST_REQUIRE(executedHeader != nullptr);
    BOOST_CHECK(!sysBlock);

    // 六字段承诺 == 直调。
    BOOST_CHECK_EQUAL(executedHeader->stateRoot(), resultA.stateRoot);
    BOOST_CHECK_EQUAL(executedHeader->txsRoot(), resultA.txRoot);
    BOOST_CHECK_EQUAL(
        executedHeader->receiptsRoot(), detail::toBcosH256(resultA.seal.receiptsRoot));
    BOOST_CHECK_EQUAL(executedHeader->gasUsed(), bcos::u256(resultA.gasUsed));
    BOOST_CHECK_EQUAL(executedHeader->withdrawalsRoot().value_or(bcos::h256{}),
        detail::toBcosH256(resultA.seal.withdrawalsRoot));
    {
        auto gotBloom = executedHeader->logsBloom();
        BOOST_REQUIRE_EQUAL(gotBloom.size(), 256u);
        BOOST_CHECK(
            std::equal(gotBloom.begin(), gotBloom.end(), std::begin(resultA.seal.logsBloom.bytes)));
    }
    if (resultA.seal.requestsHash.has_value())
    {
        BOOST_REQUIRE(executedHeader->requestsHash().has_value());
        BOOST_CHECK_EQUAL(
            executedHeader->requestsHash().value(), detail::toBcosH256(*resultA.seal.requestsHash));
    }

    // receipts/status/gasUsed 逐笔 == 直调。
    auto receipts = f.scheduler->lastExecutedReceipts();
    BOOST_REQUIRE_EQUAL(receipts.size(), resultA.receipts.size());
    for (std::size_t i = 0; i < receipts.size(); ++i)
    {
        BOOST_CHECK_EQUAL(receipts[i]->status(), resultA.receipts[i]->status());
        BOOST_CHECK_EQUAL(receipts[i]->gasUsed(), resultA.receipts[i]->gasUsed());
        BOOST_CHECK_EQUAL(std::string(receipts[i]->cumulativeGasUsed()),
            std::string(resultA.receipts[i]->cumulativeGasUsed()));
        BOOST_CHECK_EQUAL(std::string(receipts[i]->effectiveGasPrice()),
            std::string(resultA.receipts[i]->effectiveGasPrice()));
    }
}

/// 接线 Task 5c 槽位 3 E2E（SEV-10）：OpScheduler executeBlock + commitBlock 后 7 张 SYS 表落盘
/// （SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER / SYS_CURRENT_STATE /
/// SYS_NUMBER_2_TXS / SYS_HASH_2_RECEIPT / SYS_HASH_2_TX）。取代被删 OpBlockScheduler 的
/// RefuseStubs（OP 块执行/提交不再被拒绝 stub，而是真正经 OpScheduler 落盘）。
BOOST_AUTO_TEST_CASE(CommitPersistsSevenLedgerTables)
{
    Fixture f;

    // 语料信封（同 ExecutesMinimalOpBlockEqualToDirectExecuteOpBlock）：deposit + eip1559。
    auto depTx = makeDeposit();
    bcos::bytes depEnv = detail::canonicalEnvelopeBytes(bcos::evm::opstack::OpBlockTx{depTx, {}});
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, eipEnvBytes};

    auto header = makeHeader();

    // 直调 executeOpBlock 得真实承诺，回填 announced 头（verify 六字段对比通过）。
    bcos::evm::engine::OpSchedulerImpl<ViewType> directScheduler(
        f.receiptFactory, kChainId, f.forkTimestamps);
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA =
        bcos::task::syncWait(directScheduler.executeOpBlock(viewA, *header, rawTxBytes));
    BOOST_REQUIRE_EQUAL(resultA.receipts.size(), rawTxBytes.size());
    fillAnnouncedHeader(header, resultA);

    // 块装配（SEV-8 完整信封）。
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    // executeBlock → commitBlock（槽位 3 驱动）。
    bcos::Error::Ptr execErr;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            execErr = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        execErr == nullptr, "executeBlock failed: " << (execErr ? execErr->errorMessage() : ""));
    BOOST_REQUIRE(executedHeader != nullptr);

    bcos::Error::Ptr commitErr;
    called = false;
    f.scheduler->commitBlock(
        executedHeader, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            called = true;
            commitErr = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(commitErr == nullptr,
        "commitBlock failed: " << (commitErr ? commitErr->errorMessage() : ""));

    // ── 7 张表落盘断言 ──
    auto const blockNumberStr = boost::lexical_cast<std::string>(header->number());
    auto& hashImpl = *f.blockFactory->cryptoSuite()->hashImpl();
    auto view = f.multiLayerStorage.fork();
    const auto expectedBlockHash = header->opHeaderHash(bcos::engine::detail::opHeaderConst());

    // 1. SYS_NUMBER_2_HASH[number] = blockHash（announced header opHeaderHash，commit hook key）。
    auto number2Hash = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(number2Hash.has_value(), "SYS_NUMBER_2_HASH must be written");
    {
        auto const& stored = number2Hash->get();
        BOOST_REQUIRE_EQUAL(stored.size(), size_t(32));
        // Compare as hex: the entry stores raw bytes as char (signed on AppleClang), so a
        // byte-wise std::equal against h256's unsigned bytes fails for high bytes (>= 0x80).
        BOOST_CHECK_EQUAL(bcos::toHex(stored), expectedBlockHash.hex());
    }

    // 2. SYS_HASH_2_NUMBER[blockHash] = number。
    auto hash2Number = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                          bcos::concepts::bytebuffer::toView(expectedBlockHash)}));
    BOOST_REQUIRE_MESSAGE(hash2Number.has_value(), "SYS_HASH_2_NUMBER must be written");
    BOOST_CHECK_EQUAL(std::string(hash2Number->get()), blockNumberStr);

    // 3. SYS_NUMBER_2_BLOCK_HEADER[number] = tars header（非空）。
    auto headerEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(headerEntry.has_value(), "SYS_NUMBER_2_BLOCK_HEADER must be written");
    BOOST_CHECK(!headerEntry->get().empty());

    // 4. SYS_CURRENT_STATE[SYS_KEY_CURRENT_NUMBER] = number（head 推进）。
    auto currentState = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER}));
    BOOST_REQUIRE_MESSAGE(currentState.has_value(), "SYS_CURRENT_STATE head must advance");
    BOOST_CHECK_EQUAL(std::string(currentState->get()), blockNumberStr);

    // 5. SYS_NUMBER_2_TXS[number] = tx metadata（SEV-10 第 7 表）。
    auto number2Txs = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_TXS, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(number2Txs.has_value(), "SYS_NUMBER_2_TXS (SEV-10) must be written");
    BOOST_CHECK(!number2Txs->get().empty());

    // 6/7. 每笔 tx 的 SYS_HASH_2_RECEIPT + SYS_HASH_2_TX。
    for (auto const& env : rawTxBytes)
    {
        const auto txHash = hashImpl.hash(env);
        auto receiptEntry = bcos::task::syncWait(
            bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                                              bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            receiptEntry.has_value(), "SYS_HASH_2_RECEIPT must be written for tx " << txHash.hex());
        auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            txEntry.has_value(), "SYS_HASH_2_TX must be written for tx " << txHash.hex());
    }
}

/// execute hook 抛 OpConsensusError（0x03 envelope 经 decodeOneRawTx 确定性抛）→ 骨架 classify →
/// Error 码 == OpConsensusRejected。
BOOST_AUTO_TEST_CASE(ConsensusRejectionClassifiedAsOpConsensusRejected)
{
    Fixture f;

    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(makeHeader());
    auto badTx = buildUnsupportedTypeTx();
    BOOST_REQUIRE(badTx != nullptr);
    block->appendTransaction(badTx);

    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpConsensusRejected);
    BOOST_CHECK(executedHeader == nullptr);
}

/// 三分类直调：OpConsensusError→OpConsensusRejected / OpStorageError→OpStorageFault /
/// 其它→Unknown。
BOOST_AUTO_TEST_CASE(ClassifyExceptionThreeWayMapping)
{
    Fixture f;

    auto consensus = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::evm::engine::OpConsensusError{"block-level consensus"}));
    BOOST_CHECK_EQUAL(consensus, bcos::scheduler::SchedulerError::OpConsensusRejected);

    auto storage = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::evm::engine::OpStorageError{"ledger bridge poison"}));
    BOOST_CHECK_EQUAL(storage, bcos::scheduler::SchedulerError::OpStorageFault);

    auto unknown = f.scheduler->classifyException(
        std::make_exception_ptr(std::runtime_error{"generic ethereum-mode fault"}));
    BOOST_CHECK_EQUAL(unknown, bcos::scheduler::SchedulerError::UnknownError);
}

BOOST_AUTO_TEST_SUITE_END()
