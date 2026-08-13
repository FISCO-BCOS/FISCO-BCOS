// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerTest — 接线 Task 4（OpScheduler 新类）的最小 OP 块 + 三分类单测。
//
// 1. ExecutesMinimalOpBlockEqualToDirectRouteB：最小 OP 块（deposit + 1 normal 带 envelope，
//    SEV-8：extraTransactionBytes=完整信封，先例 OpDualPathEquivalenceTest.cpp:566-568）经
//    OpScheduler.executeBlock 驱动 == 直调 route B（runOpBlockInjection）结果（receipts/status/
//    gasUsed + 六字段承诺）。 语料锚：isthmus_transfer_basic.json 的 deposit + eip1559 envelope
//    （op-geth 真实签名）； 直调锚 = 同一 MLS 上独立 route B（runOpBlockInjection，route A
//    executeOpBlock 已退役——双路径 harness 已证明 route B 与其语义等价）。
// 2. ConsensusRejectionClassifiedAsOpConsensusRejected：execute hook 抛 OpConsensusError
//    （unsupported tx type byte 0x03，decodeOneRawTx 确定性抛）→ 骨架 coExecuteBlock 经
//    classifyException → Error 码 == OpConsensusRejected。
// 3. classifyException 直调三分类：OpConsensusError→OpConsensusRejected / OpStorageError→
//    OpStorageFault / 其它→UnknownError。
// 4. CommitPersistsSevenLedgerTables（Task 5c 槽位 3 E2E）：executeBlock + commitBlock 后 7 张
//    SYS 表落盘断言（SEV-10，取代被删 OpBlockScheduler 的 RefuseStubs）。
//
// 黄金约束：最小 OP 块 receipts/status/gasUsed == 直调 route B（runOpBlockInjection）。
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpTxDecode.h>  // detail::canonicalEnvelopeBytes（deposit 信封重建）

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>  // 迁移 call 用例（Task 5c fix round 1）
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
/// 由调用方在直调 route B（runOpBlockInjection）后按结果回填——announced 头即真实承诺，verify
/// 六字段对比通过。
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

/// 用 route B（runOpBlockInjection）结果回填 announced 头的承诺字段（finishExecute 也写同一批
/// 字段，verify 因此相等）。
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

// ── call/getCode 用例迁移（Task 5c fix round 1，逐字来自被删 OpBlockSchedulerTest）──
// OpScheduler 吸收 OpBlockScheduler 的 call/getCode 纯虚实现后，原 OpBlockSchedulerTest 的
// RPC 面用例（StatusAndResetNoOp / GetCodeEmpty / CallInvalidReturnsError /
// CallHappyPathInjectsRealBaseFee）迁入本套件，驱动对象换成 OpScheduler（f.scheduler）。
const bcos::Address kCallSender{"0x1000000000000000000000000000000000000000"};

/// A genesis header carrying every field coCallLatest's buildOpBlockInfo/toBlockInfo reads
/// （baseFee=1e9 是 CallHappyPath 注入断言的目标值）。
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeCallGenesisHeader()
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(0);
    h->setTimestamp(1000000);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(30000000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1000000000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Seed the minimal OP ledger the RPC call()/read paths need: current head (SYS_CURRENT_STATE)
/// and the block-0 header (getBlockData(HEADER) reads SYS_NUMBER_2_BLOCK_HEADER by number).
void seedCallGenesis(MLS& mls, bcos::protocol::BlockHeader::Ptr const& genesisHeader)
{
    auto view = mls.fork();
    view.newMutable();
    {
        bcos::storage::Entry e;
        e.set(boost::lexical_cast<std::string>(genesisHeader->number()));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
            std::move(e)));
    }
    {
        bcos::bytes buf;
        genesisHeader->encode(buf);
        bcos::storage::Entry e;
        e.set(std::move(buf));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                boost::lexical_cast<std::string>(genesisHeader->number())},
            std::move(e)));
    }
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// Build an EIP-1559 (type 2) web3 tx wrapped as a tars Transaction::Ptr（lambda-holder 形式）。
/// EIP-1559 是刻意的（round-3 C2）：EIP-2930/legacy tx with maxPriorityFeePerGas=0 会触发
/// BCOS2Evmone 的 access_list override（max_priority=max_gas），effectiveGasPrice =
/// maxFeePerGas 而非 baseFee——假阳性；EIP-1559 保持 max_priority=0，effectiveGasPrice ==
/// baseFee 精确（注入敏感）。
bcos::protocol::Transaction::Ptr buildWeb3Tx(
    bcos::u256 maxFeePerGas, bcos::u256 maxPriorityFeePerGas)
{
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 5;
    w3.nonce = 0;
    w3.maxFeePerGas = maxFeePerGas;
    w3.maxPriorityFeePerGas = maxPriorityFeePerGas;
    w3.gasLimit = 5000000;
    w3.to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    w3.value = bcos::u256(0);
    w3.signatureV = 0;
    w3.signatureR = bcos::bytes(32, 0x01);
    w3.signatureS = bcos::bytes(32, 0x02);
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
    // The dummy r/s cannot recover a sender; force it so opValidate sees the funded account.
    tx->clearSenderAndHash();
    tx->forceSender(kCallSender.asBytes());
    return tx;
}

/// Fund an EOA so opValidate passes（同 seedSender 的 create()+setCode(empty) 存在性模式）。
void fundCallAccount(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl,
    bcos::u256 const& balance)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(balance));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// route B 直调锚（route A executeOpBlock 退役后，独立执行对照改 runOpBlockInjection）：装配
/// txs/normalTxs + OpstackExecutor 后直调，返回 OpExecuteBlockResult。装配与 OpScheduler execute
/// hook（OpScheduler.h:127-179）/ OpDualPathEquivalenceTest route
/// B（:1015-1034）同款（免复制漂移）。
bcos::evm::engine::OpExecuteBlockResult runRouteBDirect(Fixture& f, ViewType& view,
    bcos::protocol::BlockHeader const& header, std::vector<bcos::bytes> const& rawTxBytes)
{
    namespace op = bcos::evm::opstack;
    const auto& cfg =
        op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, f.forkTimestamps);
    std::vector<op::OpBlockTx> txs;
    txs.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
        txs.push_back(detail::decodeOneRawTx(raw, kChainId));
    std::vector<bcos::protocol::Transaction::Ptr> normalTxs;
    normalTxs.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        if (std::holds_alternative<op::DepositTx>(txs[i].tx))
            continue;
        const auto txHash = f.hashImpl->hash(rawTxBytes[i]);
        auto tarsTx = makeConverter()(rawTxBytes[i], txHash);
        if (!tarsTx)
            throw std::runtime_error("route B direct: envelope failed opEnvelopeToTars");
        tarsTx->extraTransactionBytes.assign(rawTxBytes[i].begin(), rawTxBytes[i].end());
        normalTxs.push_back(std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; }));
    }
    bcos::ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setEVMCRevision(cfg.rev);
    bcos::executor_v1::opstack::OpstackExecutor executor{f.receiptFactory, f.hashImpl, cfg};
    return bcos::evm::engine::runOpBlockInjection(executor, view, header, txs, normalTxs, cfg,
        kChainId, ledgerConfig, rawTxBytes, f.hashImpl);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSuite)

/// 黄金约束：最小 OP 块（deposit + 1 normal 带 envelope）经 OpScheduler == 直调 route B。
BOOST_AUTO_TEST_CASE(ExecutesMinimalOpBlockEqualToDirectRouteB)
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

    // 直调锚：同一 MLS 上独立 route B（runOpBlockInjection），同一 header 环境。
    // （route A executeOpBlock 已退役——双路径 harness 已证明 route B 与其语义等价。）
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA;
    try
    {
        resultA = runRouteBDirect(f, viewA, *header, rawTxBytes);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR("direct runOpBlockInjection threw: " << e.what());
        return;
    }
    catch (...)
    {
        BOOST_ERROR("direct runOpBlockInjection threw an unknown (RTTI-bypassed) exception");
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

    // Task 6（P4 M3）黄金约束：executeBlock（骨架驱动，execute hook = runOpBlockInjection
    // route B）的完整结果 == 直调 runOpBlockInjection（route B）——peekExecuteResult 暴露骨架
    // m_results 里的原始结果。
    auto routeB = f.scheduler->peekExecuteResult();
    BOOST_REQUIRE_MESSAGE(routeB.has_value(), "executeBlock must stash an OpExecuteBlockResult");
    BOOST_CHECK_EQUAL(routeB->stateRoot, resultA.stateRoot);
    BOOST_CHECK_EQUAL(routeB->txRoot, resultA.txRoot);
    BOOST_CHECK_EQUAL(routeB->gasUsed, resultA.gasUsed);
    // evmc::bytes32 / BloomFilter 无 operator<<，用 == / 字节比较（不可 BOOST_CHECK_EQUAL）。
    BOOST_CHECK(routeB->seal.receiptsRoot == resultA.seal.receiptsRoot);
    BOOST_CHECK(std::equal(std::begin(routeB->seal.logsBloom.bytes),
        std::end(routeB->seal.logsBloom.bytes), std::begin(resultA.seal.logsBloom.bytes)));
    BOOST_CHECK(routeB->seal.withdrawalsRoot == resultA.seal.withdrawalsRoot);
    BOOST_CHECK_EQUAL(routeB->seal.requestsHash.has_value(), resultA.seal.requestsHash.has_value());
    if (routeB->seal.requestsHash.has_value() && resultA.seal.requestsHash.has_value())
        BOOST_CHECK(*routeB->seal.requestsHash == *resultA.seal.requestsHash);  // evmc::bytes32 无
                                                                                // <<
    BOOST_CHECK_EQUAL(routeB->seal.blobGasUsed.has_value(), resultA.seal.blobGasUsed.has_value());
    if (routeB->seal.blobGasUsed.has_value() && resultA.seal.blobGasUsed.has_value())
        BOOST_CHECK_EQUAL(*routeB->seal.blobGasUsed, *resultA.seal.blobGasUsed);
}

/// 接线 Task 5c 槽位 3 E2E（SEV-10）：OpScheduler executeBlock + commitBlock 后 7 张 SYS 表落盘
/// （SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER / SYS_CURRENT_STATE /
/// SYS_NUMBER_2_TXS / SYS_HASH_2_RECEIPT / SYS_HASH_2_TX）。取代被删 OpBlockScheduler 的
/// RefuseStubs（OP 块执行/提交不再被拒绝 stub，而是真正经 OpScheduler 落盘）。
BOOST_AUTO_TEST_CASE(CommitPersistsSevenLedgerTables)
{
    Fixture f;

    // 语料信封（同 ExecutesMinimalOpBlockEqualToDirectRouteB）：deposit + eip1559。
    auto depTx = makeDeposit();
    bcos::bytes depEnv = detail::canonicalEnvelopeBytes(bcos::evm::opstack::OpBlockTx{depTx, {}});
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, eipEnvBytes};

    auto header = makeHeader();

    // 直调 route B 得真实承诺，回填 announced 头（verify 六字段对比通过）。
    // （route A executeOpBlock 已退役——双路径 harness 已证明 route B 与其语义等价。）
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA =
        runRouteBDirect(f, viewA, *header, rawTxBytes);
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

// ── RPC 面用例迁移（Task 5c fix round 1，逐字来自被删 OpBlockSchedulerTest；驱动对象换成
//    OpScheduler——f.scheduler 是 TestOpScheduler<OpScheduler>，call/getCode/status/reset 继承）──

/// 骨架默认 no-op status/reset（OpBlockScheduler 同语义）。
BOOST_AUTO_TEST_CASE(StatusAndResetNoOp)
{
    Fixture f;
    f.scheduler->status([&](bcos::Error::Ptr err, bcos::protocol::Session::ConstPtr) {
        BOOST_REQUIRE(err == nullptr);
    });
    f.scheduler->reset([&](bcos::Error::Ptr err) { BOOST_REQUIRE(err == nullptr); });
}

/// 未知地址 → 空 code，无错误（getCode 只读 features，不走 getLedgerConfig，OP 头空 dataHash
/// 不触碰 BlockHeader::hash()）。
BOOST_AUTO_TEST_CASE(GetCodeEmpty)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    bool called = false;
    f.scheduler->getCode(
        "0x0000000000000000000000000000000000000001", [&](bcos::Error::Ptr err, bcos::bytes code) {
            called = true;
            BOOST_REQUIRE(err == nullptr);
            BOOST_REQUIRE(code.empty());
        });
    BOOST_REQUIRE(called);
}

/// 无效调用（maxFeePerGas=1 < baseFee(1e9) trips evmone validate FEE_CAP_LESS_THAN_BLOCKS；未
/// 资金 sender 亦 trip balance check）→ JSON-RPC Error，绝不 status-0 receipt。
BOOST_AUTO_TEST_CASE(CallInvalidReturnsError)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    auto tx = buildWeb3Tx(/*maxFeePerGas=*/1, /*maxPriorityFeePerGas=*/0);
    bool called = false;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr err, bcos::protocol::TransactionReceipt::Ptr) {
            called = true;
            BOOST_REQUIRE(err != nullptr);  // Error (JSON-RPC)，绝不 status-0 receipt
        });
    BOOST_REQUIRE(called);
}

/// 调度器级 call 链路（OpScheduler::call → coCallLatest → buildOpBlockInfo）的 baseFee 注入对拍
/// （Task 5/dual-path #34）：maxPriorityFeePerGas=0（EIP-1559，BCOS2Evmone access_list override
/// 不触发）→ effectiveGasPrice == base_fee + min(0, maxFee-base_fee) == base_fee 精确。pre-fix
/// buildOpBlockInfo 注入 base_fee=0 → egp "0x0"；post-fix header baseFee(1e9) 透出——证明 Task 4
/// buildOpBlockInfo baseFee 修复在调度器级 call 链路生效。
BOOST_AUTO_TEST_CASE(CallHappyPathInjectsRealBaseFee)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);

    auto tx = buildWeb3Tx(
        /*maxFeePerGas=*/bcos::u256(30'000'000'000ULL), /*maxPriorityFeePerGas=*/0);

    bcos::protocol::TransactionReceipt::Ptr got;
    bcos::Error::Ptr err;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr e, bcos::protocol::TransactionReceipt::Ptr r) {
            err = std::move(e);
            got = std::move(r);
        });
    BOOST_REQUIRE_MESSAGE(
        err == nullptr, "eth_call must succeed, got error: " << (err ? err->errorMessage() : ""));
    BOOST_REQUIRE(got != nullptr);
    // effectiveGasPrice 是 hex 字符串（"0x...", TransactionReceipt.h:75）。解析成 u256 并断言
    // == header baseFee(1e9)——精确（EIP-1559 + maxPriority=0 → effectiveGasPrice == baseFee）。
    const auto egp = bcos::u256(std::string(got->effectiveGasPrice()));
    const auto baseFee = bcos::u256(1'000'000'000);
    BOOST_CHECK_MESSAGE(
        egp == baseFee, "effectiveGasPrice " << egp << " must equal header baseFee " << baseFee);
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
