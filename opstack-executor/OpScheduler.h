// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — OP 专用调度器（spec 2026-08-12-op-baseline-scheduler-wiring 接线 Task 4）。
// 继承 SchedulerSkeleton（统一编排骨架），实现 OP 的 5 个 CRTP hook + 4 个纯虚
// （call/getCode/getABI/getPendingStorageAt）+ classifyException；锁/连续性/背压/view/
// 队列/notifier/流程全部继承，无重复。status/reset/preExecuteBlock 用骨架默认（v3 P1-7）。
//
// 层：模板头（template<class MultiLayerStorage>）——真实 GlobalStateStorage 由 composition root
// （Initializer）在槽位 3 装配时实例化；opstack-executor 不 link libinitializer，故不在此命名
// GlobalStateStorage（与 OpBlockScheduler 的 `<ViewType, OpenedStorage>` 模板先例同构）。
//
// 与引擎 seam 的关系：引擎 SchedulerType 仍为 OpSchedulerImpl（computeTxRoot/… 依赖名不变）；
// 本类内部持有一个 OpSchedulerImpl 实例（m_schedulerImpl，构造即建 evmc::VM），execute hook 走
// executeOpBlock（route A）。P4 换芯（route B runOpBlockInjection）只动 execute hook。
//
// EnvelopeToTarsConverter 由 composition root 注入（v4 P0-2：opstack-executor 不 link engine——
// opEnvelopeToTars 在 engine lib；复用 Task 1 带的 using 别名 OpBlockRegister.h:24）。

#include <opstack-executor/OpBlockRegister.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <transaction-scheduler/bcos-transaction-scheduler/SchedulerSkeleton.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>  // readFromStorage
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-ledger/LedgerMethods.h>  // getCurrentBlockNumber / getBlockData CPO tag_invoke
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <ethereum-executor/StorageStateView.h>
#include <boost/algorithm/hex.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::executor_v1::opstack
{
/// OP block execution payload carried through the shared `SchedulerExecuteResult.modeExtra` to
/// the commit hook (v4 P1-4): the raw EIP-2718 envelopes plus the execution result — exactly what
/// `opstackRegisterBlock` needs (rawTxBytes + result + blockFactory + envelopeToTars).
template <class MultiLayerStorage>
class OpScheduler : public bcos::scheduler_v1::SchedulerSkeleton<MultiLayerStorage,
                        bcos::executor_v1::opstack::OpstackExecutor,
                        bcos::evm::engine::OpSchedulerImpl<typename MultiLayerStorage::ViewType>,
                        bcos::ledger::LedgerInterface, OpScheduler<MultiLayerStorage>>
{
    using ViewType = typename MultiLayerStorage::ViewType;
    using SchedulerBase = bcos::scheduler_v1::SchedulerSkeleton<MultiLayerStorage,
        bcos::executor_v1::opstack::OpstackExecutor, bcos::evm::engine::OpSchedulerImpl<ViewType>,
        bcos::ledger::LedgerInterface, OpScheduler<MultiLayerStorage>>;

    /// What the execute hook stashes in `SchedulerExecuteResult::modeExtra`.
    struct OpExecuteExtra
    {
        bcos::evm::engine::OpExecuteBlockResult result;
        std::vector<bcos::bytes> rawTxBytes;
    };

public:
    // 5 CRTP hooks + 4 纯虚 + classify 置 public（CRTP 非虚分派：基类经 derived() 需访问派生
    // 定义；与 BaselineScheduler 同，Task 3b 已定）。status/reset/preExecuteBlock 用骨架默认。
    /// ① 交易来源：block.transactions() → Transaction::ConstPtr（跳过 txpool——OP 块内联交易）。
    /// v3（SEV-8）：extraTransactionBytes 是 signing preimage——P3 块装配已覆写为完整信封，
    /// execute hook 从它提取 raw envelope 供 executeOpBlock。
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, ViewType& /*view*/)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    /// ② 执行内核：executeOpBlock（route A，OpSchedulerImpl.h:202）。
    /// → 包 SchedulerExecuteResult{receipts, modeExtra=OpExecuteExtra{result, rawTxBytes}}。
    task::Task<bcos::scheduler_v1::SchedulerExecuteResult> execute(ViewType& view,
        protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        std::vector<bcos::bytes> rawTxBytes;
        rawTxBytes.reserve(transactions.size());
        for (auto const& tx : transactions)
        {
            auto const& env = tx->extraTransactionBytes();
            rawTxBytes.emplace_back(env.begin(), env.end());
        }

        bcos::evm::engine::OpExecuteBlockResult result;
        try
        {
            result = co_await m_schedulerImpl.executeOpBlock(view, header, rawTxBytes);
        }
        catch (const std::exception&)
        {
            throw;  // 可绑定类型（logic_error 族 / 正常 typeinfo 的 runtime_error）→ 骨架
                    // classify。
        }
        catch (...)
        {
            // RTTI 旁路（Storage2State.h:195-199 实测：runtime_error 子类逃 typed catch，落到
            // catch(...)）——decodeOneRawTx 的 OpConsensusError 等以坏 typeinfo 逃出 executeOpBlock
            // 会绕过骨架 coExecuteBlock 的 catch(std::exception&)（实测：直接抛到测试）。
            // 归一为 FISCO 类型（consensus——storage 故障已被 executeOpBlock 的 poison-first
            // 归为 OpStorageError 抛在可绑定路径），保证 classifyException 收到可 catch 类型。
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: executeOpBlock threw an unrecognized (RTTI-bypassed) exception; "
                "raw tx decode or block-level consensus fault");
        }

        bcos::scheduler_v1::SchedulerExecuteResult out;
        out.receipts = std::move(result.receipts);
        out.modeExtra = std::make_shared<OpExecuteExtra>(
            OpExecuteExtra{std::move(result), std::move(rawTxBytes)});
        co_return out;
    }

    /// ③ finish：OP 承诺写 executedHeader（setStateRoot/TxsRoot/ReceiptsRoot/GasUsed/LogsBloom/
    /// WithdrawalsRoot/RequestsHash/BlobGasUsed；跳过 MPT；不调 BlockHeader::hash()）。
    /// （v3 A8：新建机制——当前 OP 无"计算值回写头"代码，头由 payload announced 重建 + 对比验证。）
    task::Task<protocol::BlockHeader::Ptr> finishExecute(ViewType& /*view*/,
        bcos::scheduler_v1::SchedulerExecuteResult& result,
        protocol::BlockHeader const& blockHeader, protocol::Block& /*block*/,
        std::vector<protocol::Transaction::ConstPtr> const& /*transactions*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool& sysBlock)
    {
        namespace detail = bcos::evm::engine::detail;
        sysBlock = false;
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(result.modeExtra);
        auto const& opResult = extra.result;

        auto executedBlockHeader = this->m_blockFactory->blockHeaderFactory()->populateBlockHeader(
            protocol::BlockHeader::ConstPtr{&blockHeader, [](protocol::BlockHeader const*) {}});
        executedBlockHeader->setStateRoot(opResult.stateRoot);
        executedBlockHeader->setTxsRoot(opResult.txRoot);
        executedBlockHeader->setReceiptsRoot(detail::toBcosH256(opResult.seal.receiptsRoot));
        executedBlockHeader->setGasUsed(bcos::u256(opResult.gasUsed));
        auto const& bloom = opResult.seal.logsBloom;
        executedBlockHeader->setLogsBloom(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes)));
        executedBlockHeader->setWithdrawalsRoot(detail::toBcosH256(opResult.seal.withdrawalsRoot));
        if (opResult.seal.requestsHash.has_value())
            executedBlockHeader->setRequestsHash(detail::toBcosH256(*opResult.seal.requestsHash));
        if (opResult.seal.blobGasUsed.has_value())
            executedBlockHeader->setBlobGasUsed(bcos::u256(*opResult.seal.blobGasUsed));
        co_return executedBlockHeader;
    }

    /// ④ verify：六字段对比（mismatchedFieldOf，Task 1 seam，无条件——verify 门控在 hook 内，
    /// OP 恒比较）；mismatch 抛 OpConsensusError(mismatchedField)，经骨架 classify →
    /// OpConsensusRejected。返回 null = 通过。
    task::Task<Error::Ptr> verifyResult(protocol::BlockHeader::Ptr executed,
        protocol::BlockHeader const& announced, bool /*verify*/)
    {
        namespace engine = bcos::evm::engine;
        if (auto mismatch = engine::mismatchedFieldOf(
                headerCommitments(*executed), headerCommitments(announced)))
        {
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: six-way commitment mismatch on field " + *mismatch);
        }
        co_return nullptr;
    }

    /// ⑤ commit：opstackRegisterBlock（Task 1 机制）写独立 MutableStorage → 返回之
    /// （骨架唯一一次 mergeBackStorage，FIB-104）。blockHash 经 header.opHeaderHash(opHeaderConst)
    /// ——OP 头 hash 在 codec，不调 BlockHeader::hash()（A8）。
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        ViewType& /*view*/, protocol::BlockHeader::Ptr header,
        bcos::scheduler_v1::SchedulerExecuteResult const& result)
    {
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(result.modeExtra);
        auto storage = std::make_shared<typename MultiLayerStorage::MutableStorage>();

        bcos::protocol::BlockHeader::OpHeaderConst opHeaderConst{
            .ommersHash = c_emptyOmmersHash,
            .difficulty = bcos::u256(0),
            .nonce = c_posNonce,
        };
        auto blockHash = header->opHeaderHash(opHeaderConst);

        co_await bcos::evm::engine::opstackRegisterBlock(*storage, *header, blockHash,
            extra.rawTxBytes, extra.result, *this->m_blockFactory, m_envelopeToTars);
        co_return storage;
    }

    // ---- 纯虚：call / 存储读（v3 B3：骨架不实现，派生各实现） ----

    /// eth_call：复刻 OpBlockScheduler::call（coCallLatest，10 参注入 + 手搓 LedgerConfig +
    /// 双 catch + RTTI-bypass）。错误经回调返回 JSON-RPC Error，绝不 status-0 receipt。
    void call(protocol::Transaction::Ptr transaction,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait([this, tx = std::move(transaction),
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            try
            {
                cb(nullptr, co_await coCallLatest(std::move(tx)));
            }
            catch (const std::exception& e)
            {
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()),
                    nullptr);
            }
            catch (...)
            {
                // Typed-catch RTTI bypass（同 OpBlockScheduler.h:104-118 注释）：-fno-rtti 的
                // libevmone.a 带隐藏 typeinfo，catch(const std::exception&) 不可靠绑定
                // runtime_error——兜底返回 Error，不悬挂 RPC 协程。
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                       "OpScheduler::call: unknown (RTTI-bypassed) exception"),
                    nullptr);
            }
        }());
    }

    /// getCode：存储读走 readFromStorage 模式（不走 getLedgerConfig——header.hash() 对 OP 头抛
    /// EmptyBlockHeaderHash）。
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_multiLayerStorage->fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                bcos::ledger::Features features;
                co_await bcos::ledger::readFromStorage(features, view, blockNumber);

                bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                    features.get(bcos::ledger::Features::Flag::feature_raw_address));
                auto code = co_await account.code();
                if (!code)
                {
                    callback(nullptr, {});
                    co_return;
                }
                auto bytesView = code->get();
                callback(nullptr, bcos::bytes(bytesView.begin(), bytesView.end()));
            }
            catch (const std::exception& e)
            {
                callback(
                    BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()), {});
            }
        }(this, contract, std::move(callback)));
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_multiLayerStorage->fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                bcos::ledger::Features features;
                co_await bcos::ledger::readFromStorage(features, view, blockNumber);

                bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                    features.get(bcos::ledger::Features::Flag::feature_raw_address));
                auto abi = co_await account.abi();
                if (!abi)
                {
                    callback(nullptr, {});
                    co_return;
                }
                callback(nullptr, std::string(abi->get()));
            }
            catch (const std::exception& e)
            {
                callback(
                    BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()), {});
            }
        }(this, contract, std::move(callback)));
    }

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) override
    {
        auto view = this->m_multiLayerStorage->fork();
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        bcos::ledger::account::EVMAccount account(
            view, address, features.get(bcos::ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(key);
    }

    // A3：异常分类——OpConsensusError→OpConsensusRejected / OpStorageError→OpStorageFault /
    // 其它→UnknownError。类型识别 rethrow + catch（前置：executeOpBlock 内已归一为 FISCO 类型，
    // 跨 -fno-rtti evmone 边界的 RTTI 陷阱由 executeOpBlock 的 catch(...) 兜底处理，v3 P1-6）。
    scheduler::SchedulerError classifyException(std::exception_ptr eptr) const override
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            return scheduler::SchedulerError::OpConsensusRejected;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (...)
        {
            return scheduler::SchedulerError::UnknownError;
        }
    }

    OpScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps,
        bcos::protocol::BlockFactory::Ptr blockFactory, MultiLayerStorage& multiLayerStorage,
        bcos::evm::engine::EnvelopeToTarsConverter envelopeToTars)
      : SchedulerBase(),
        m_schedulerImpl(receiptFactory, chainId, forkTimestamps),
        m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_envelopeToTars(std::move(envelopeToTars))
    {
        this->m_multiLayerStorage = &multiLayerStorage;
        this->m_blockFactory = blockFactory.get();
    }
    OpScheduler(const OpScheduler&) = delete;
    OpScheduler& operator=(const OpScheduler&) = delete;
    ~OpScheduler() noexcept override = default;

public:
    /// OP 路径不能走骨架默认的 getLedgerConfig（LedgerMethods.h:347 调 header.hash()，OP 头
    /// dataHash 为空抛 EmptyBlockHeaderHash）——手搓 LedgerConfig（features only；execute hook 的
    /// executeOpBlock 不经 LedgerConfig——它吃 header + fork schedule）。
    /// 置 public：骨架 coExecuteBlock 经 derived()（CRTP）访问派生定义，protected 会触发访问错误
    /// （与 5 hook 同因，Task 3b）。
    task::Task<ledger::LedgerConfig::Ptr> loadLedgerConfig(
        ViewType& view, protocol::BlockNumber number)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(number);
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        ledgerConfig->setFeatures(features);
        co_return ledgerConfig;
    }

    /// 同上：骨架默认经 header->hash() + getLedgerConfig(*m_ledger)——OP commit 路径不经
    /// header.hash()（M4）。置 public 同 loadLedgerConfig（coCommitBlock 经 derived() 访问）。
    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setTimestamp(header->timestamp());
        co_return ledgerConfig;
    }

private:
    /// Project an executed/announced OP header's commitment fields into the six-way comparison
    /// surface. Both sides read the same header accessors so `mismatchedFieldOf` is comparing the
    /// values finishExecute wrote (executed) against what the payload announced.
    static bcos::evm::engine::OpBlockCommitments headerCommitments(protocol::BlockHeader const& h)
    {
        namespace detail = bcos::evm::engine::detail;
        auto bloom = h.logsBloom();
        bcos::h2048 logsBloom(reinterpret_cast<const bcos::byte*>(bloom.data()), bloom.size());
        std::optional<uint64_t> blobGasUsed;
        if (auto bg = h.blobGasUsed())
            blobGasUsed = detail::narrowU256ToU64(*bg, "headerCommitments blobGasUsed");
        return bcos::evm::engine::OpBlockCommitments{
            .receiptsRoot = h.receiptsRoot(),
            .logsBloom = logsBloom,
            .withdrawalsRoot = h.withdrawalsRoot().value_or(bcos::h256{}),
            .stateRoot = h.stateRoot(),
            .gasUsed = h.gasUsed(),
            .txRoot = h.txsRoot(),
            .blobGasUsed = blobGasUsed,
            .requestsHash = h.requestsHash(),
        };
    }

    /// Strict hex-address parse for getCode/getABI（同 OpBlockScheduler::parseAddress）。
    static evmc_address parseAddress(std::string_view view)
    {
        evmc_address out{};
        if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'))
            view.remove_prefix(2);
        if (view.size() != sizeof(out.bytes) * 2)
            throw std::invalid_argument("OpScheduler: invalid address (need 40 hex chars)");
        boost::algorithm::unhex(view.begin(), view.end(), out.bytes);
        return out;
    }

    /// OP eth_call：fork 最新已提交 state，建真实 OP block context（手搓 LedgerConfig，非
    /// getLedgerConfig），load L1Block fee 参数，跑 OpstackExecutor::executeTransaction
    /// （注入 chainId/blockGasLeft/block hashes），丢 fork（dry-run）。复刻 OpBlockScheduler::
    /// coCallLatest（OpBlockScheduler.h:259-306）。
    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;
        namespace detail = bcos::evm::engine::detail;

        auto view = this->m_multiLayerStorage->fork();
        view.newMutable();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto block = co_await bcos::ledger::getBlockData(
            view, blockNumber, bcos::ledger::HEADER, *this->m_blockFactory);
        // 保持 header Ptr 存活：blockHeader() 按值返回 fresh shared_ptr，引用会悬空。
        auto blockHeader = block->blockHeader();
        auto const& header = *blockHeader;

        const auto& cfg =
            op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

        auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(blockNumber);
        ledgerConfig->setTimestamp(header.timestamp());
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, blockNumber);
        ledgerConfig->setFeatures(features);
        ledgerConfig->setEVMCRevision(cfg.rev);

        eth::StorageStateView<ViewType> stateView(view);
        auto fee = op::loadOpFeeParams(stateView);
        const auto blockGasLeft = static_cast<int64_t>(
            detail::narrowU256ToU64(header.gasLimit(), "OpScheduler blockGasLeft"));

        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<ViewType> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        bcos::executor_v1::opstack::OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

        auto receipt = co_await executor.executeTransaction(view, header, *transaction,
            /*contextID=*/0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        if (hashErr.has_value())
            throw std::runtime_error("OpScheduler: block-hash lookup failed: " + *hashErr);
        co_return receipt;
    }

    // 3 post-merge OP header constants（EngineServiceImpl.cpp:81-83 同值，供 commit 的
    // opHeaderHash 用——引擎经 detail::opHeaderConst() 注入，opstack-executor 不 link engine）。
    static const bcos::h256 c_emptyOmmersHash;
    static const bcos::h64 c_posNonce;

    bcos::evm::engine::OpSchedulerImpl<ViewType> m_schedulerImpl;
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
    bcos::evm::engine::EnvelopeToTarsConverter m_envelopeToTars;
};

template <class MultiLayerStorage>
const bcos::h256 OpScheduler<MultiLayerStorage>::c_emptyOmmersHash{
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}};
template <class MultiLayerStorage>
const bcos::h64 OpScheduler<MultiLayerStorage>::c_posNonce{std::string{"0x0000000000000000"}};

}  // namespace bcos::executor_v1::opstack
