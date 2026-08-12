// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — OP 专用调度器（spec 2026-08-12-op-baseline-scheduler-wiring 接线 Task 4 + P4）。
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
// executeOpBlock（route A）。Task 6（P4 M3）换芯：execute hook 改走 runOpBlockInjection
// （route B，OpBlockInjector.h:31，逐笔注入循环），route A 保留为注释对照的 fallback。
//
// EnvelopeToTarsConverter 由 composition root 注入（v4 P0-2：opstack-executor 不 link engine——
// opEnvelopeToTars 在 engine lib；复用 Task 1 带的 using 别名 OpBlockRegister.h:24）。

#include <opstack-executor/OpBlockInjector.h>  // runOpBlockInjection（route B，Task 6 P4 M3）
#include <opstack-executor/OpBlockRegister.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/OpstackExecutorCache.h>  // 分叉键缓存（SEV-9）
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
        /// The block's OP hash (announced header's `opHeaderHash`) — stashed at execute time so
        /// the commit hook's opstackRegisterBlock keys the tables by the authoritative block
        /// hash without recomputing it from the (commitment-only) executed header. finishExecute
        /// deliberately fills only the commitment fields (A8); the executed header's optional
        /// header fields (baseFee/excessBlobGas/parentBeaconBlockRoot/...) stay empty, so
        /// `opHeaderHash` on it throws std::bad_optional_access (Task 5b wiring finding — the
        /// commit hook was never driven until the delegate path).
        bcos::crypto::HashType announcedBlockHash;
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

    /// ② 执行内核：route B——runOpBlockInjection（逐笔注入循环，OpBlockInjector.h:31，Task 6
    /// P4 M3 换芯）。装配：rawTxBytes = 各笔 extraTransactionBytes（P3 块装配已覆写完整信封，
    /// SEV-8）；txs = detail::decodeOneRawTx(chainId)；normalTxs = EnvelopeToTarsConverter
    /// （composition-root 注入的 lambda，经构造传入——转换结果须覆写 extraTransactionBytes =
    /// 完整信封，opEnvelopeToTars 不设该字段，先例 EngineServiceImpl.h:1192 /
    /// OpDualPathEquivalenceTest.cpp:566-568）；cfg = configAt(timestamp/1000, forkTimestamps)；
    /// executor = OpstackExecutorCache 分叉键取（SEV-9，不每调现构造）；ledgerConfig 只需
    /// evmcRevision（injector 的 executeDeposit/executeTransaction/finalizeBlock 校验它）。
    /// route A（executeOpBlock，OpSchedulerImpl.h:202）保留为注释对照的 fallback——换芯前该
    /// hook 即 `co_await m_schedulerImpl.executeOpBlock(view, header, rawTxBytes)`（m_schedulerImpl
    /// 成员保留），如需回退恢复注释体即可。
    /// → 包 SchedulerExecuteResult{receipts, modeExtra=OpExecuteExtra{result, rawTxBytes}}。
    task::Task<bcos::scheduler_v1::SchedulerExecuteResult> execute(ViewType& view,
        protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        namespace op = bcos::evm::opstack;
        namespace detail = bcos::evm::engine::detail;

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
            // cfg：forkTimestamps 解析（与 executeOpBlock 同源 configAt；tars 毫秒 → OP 秒）。
            const auto& cfg =
                op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

            // txs：排序/解码（decodeOneRawTx 内部含 whole-envelope canonical round-trip，P4
            // 兜底）。
            std::vector<op::OpBlockTx> txs;
            txs.reserve(rawTxBytes.size());
            for (auto const& raw : rawTxBytes)
                txs.push_back(detail::decodeOneRawTx(raw, m_chainId));

            // normalTxs：converter 逐笔（跳过 deposit、按块内序）——对齐 injector 的 normalIdx
            // 仅非 deposit 分支 ++（OpBlockInjector.h:71）。转换成功与否决定 extraTransactionBytes
            // 覆写（SEV-8 同上）。转换失败：到达执行的 envelope 已通过引擎 step-2 静态校验
            // （canonical + enumerated），是本地故障——与 engine buildOpBlock 的
            // OpExecutionInternalError 同语义（EngineServiceImpl.h:1186），非对块的裁决。
            std::vector<protocol::Transaction::Ptr> normalTxs;
            normalTxs.reserve(rawTxBytes.size());
            for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
            {
                if (std::holds_alternative<op::DepositTx>(txs[i].tx))
                    continue;
                const auto txHash = m_hashImpl->hash(rawTxBytes[i]);
                auto tarsTx = m_envelopeToTars(rawTxBytes[i], txHash);
                if (!tarsTx)
                {
                    BOOST_THROW_EXCEPTION(
                        bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                            "OpScheduler: envelope failed opEnvelopeToTars conversion"});
                }
                tarsTx->extraTransactionBytes.assign(rawTxBytes[i].begin(), rawTxBytes[i].end());
                normalTxs.push_back(std::make_shared<bcostars::protocol::TransactionImpl>(
                    [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; }));
            }

            bcos::ledger::LedgerConfig ledgerConfig;
            ledgerConfig.setEVMCRevision(cfg.rev);

            auto& executor = m_executorCache.get(m_forkTimestamps, m_chainId, cfg);

            result = bcos::evm::engine::runOpBlockInjection(executor, view, header, txs, normalTxs,
                cfg, m_chainId, ledgerConfig, rawTxBytes, m_hashImpl);
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            // FISCO 类型 typeinfo 稳定，可绑定——原样重抛，保留原始消息（describeException 在
            // 骨架 catch(...) 兜底处恢复它）。不落到 catch(std::exception&)（-fno-rtti evmone
            // 边界 std::exception typeinfo 非唯一，不可靠绑定）或 catch(...)（消息丢失）。
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            // 同上：storage 故障保留类型与消息——骨架 classifyException 正确映射
            // OpStorageFault（-32603，非 INVALID）。
            throw;
        }
        catch (const std::exception&)
        {
            throw;  // 可绑定类型（logic_error 族 / 正常 typeinfo 的 runtime_error）→ 骨架
                    // classify。
        }
        catch (...)
        {
            // RTTI 旁路（Storage2State.h:195-199 实测：runtime_error 子类逃 typed catch，落到
            // catch(...)）——decodeOneRawTx 的 OpConsensusError 等以坏 typeinfo 逃出本 hook
            // 会绕过骨架 coExecuteBlock 的 catch(std::exception&)（实测：直接抛到测试）。
            // 归一为 FISCO 类型（consensus——storage 故障已被 runOpBlockInjection 的
            // poison-first 归为 OpStorageError 抛在可绑定路径），保证 classifyException 收到
            // 可 catch 类型。
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: runOpBlockInjection threw an unrecognized (RTTI-bypassed) exception; "
                "raw tx decode or block-level consensus fault");
        }

        bcos::scheduler_v1::SchedulerExecuteResult out;
        // 拷贝（shared_ptr vector，廉价）而非 move：`SchedulerExecuteResult.receipts`（骨架
        // pushResult / 回调面）与 `OpExecuteExtra.result.receipts`（commit hook 的
        // opstackRegisterBlock 校验 rawTxBytes.size() !=
        // result.receipts.size()，OpBlockRegister.h:113）都要持有完整 receipts。
        out.receipts = result.receipts;
        // OP 无 txpool 提交——置非空空表，避免骨架 coCommitBlock 的 notifyBlockNumber 对
        // `*result->m_transactions` 空指针解引用（SchedulerSkeleton.h:355）。空表使 tx-submit zip
        // 空循环，blockNumber/transaction notifier 仍正常走。
        out.m_transactions = std::make_shared<protocol::ConstTransactions>();
        // announcedBlockHash: 权威块 hash（announced header 的 opHeaderHash，引擎 step 2 已校验）
        // 存进 extra——commit hook 用它 key 表，不在 executed header 上重算（其可选字段不全）。
        out.modeExtra = std::make_shared<OpExecuteExtra>(
            OpExecuteExtra{std::move(result), std::move(rawTxBytes),
                header.opHeaderHash(
                    bcos::protocol::BlockHeader::OpHeaderConst{.ommersHash = c_emptyOmmersHash,
                        .difficulty = bcos::u256(0),
                        .nonce = c_posNonce})});
        co_return out;
    }

    /// 覆写骨架 fastPathHit（I-2 硬契约守卫）。骨架 base 只按块号命中（SchedulerSkeleton.h）；
    /// OP 链上块号不能唯一标识块——若上一块 execute 成功但 commit 失败（opstackRegisterBlock /
    /// mergeBackStorage 抛），result 滞留 m_results、view 滞留层栈，而 SYS_HASH_2_NUMBER /
    /// SYS_NUMBER_2_HASH 均未写（引擎 step 3b/3c 都拦不住该同高度重送）。此时 op-node 若送同
    /// 高度另一块，base 会命中旧块 executedHeader、跳过新块执行 + verify，commit 写旧块状态却
    /// 报新载荷 VALID —— 链上状态与 op-node 分叉。本覆写在命中时追加比对：缓存块（execute hook
    /// 存的 OpExecuteExtra::announcedBlockHash）必须等于入块 announced header 的 opHeaderHash，
    /// 不符返回无命中（强制重执行）。范围匹配逻辑与 base 逐字一致，在单锁内完成（消除
    /// base-hit 与校验间的 TOCTOU）。返回无命中后骨架 fallthrough 到连续性复查，拒绝该高度的
    /// 重复块（-32603，诚实拒绝而非假 VALID）。
    std::optional<std::pair<protocol::BlockHeader::Ptr, bool>> fastPathHit(
        protocol::BlockNumber number, protocol::BlockHeader const& announcedHeader)
    {
        std::unique_lock resultsLock(this->m_resultsMutex);
        if (this->m_results.empty())
        {
            return std::nullopt;
        }
        auto frontNumber = this->m_results.front()->m_executedBlockHeader->number();
        auto backNumber = this->m_results.back()->m_executedBlockHeader->number();
        if (number <= frontNumber && number >= backNumber)
        {
            auto& result = this->m_results.at(frontNumber - number);
            auto extra = std::static_pointer_cast<OpExecuteExtra>(result->modeExtra);
            if (!extra ||
                extra->announcedBlockHash !=
                    announcedHeader.opHeaderHash(
                        bcos::protocol::BlockHeader::OpHeaderConst{.ommersHash = c_emptyOmmersHash,
                            .difficulty = bcos::u256(0),
                            .nonce = c_posNonce}))
            {
                // Different block (or a result lacking the OP extra) at this height: the cached
                // executedHeader must not stand in for the new block.
                BASELINE_SCHEDULER_LOG(INFO) << "Fast-path cache holds a different block at height "
                                             << number << "; ignoring cache and re-executing";
                return std::nullopt;
            }
            BASELINE_SCHEDULER_LOG(INFO) << "Block has been executed, return result directly";
            return std::pair{result->m_executedBlockHeader, result->m_sysBlock};
        }
        return std::nullopt;
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
    /// （骨架唯一一次 mergeBackStorage，FIB-104）。blockHash 用 execute hook 存的
    /// extra.announcedBlockHash（announced header 的 opHeaderHash，引擎 step 2 已校验）——
    /// 不在 executed header 上重算（finishExecute 只填承诺字段，可选头字段不全会抛
    /// bad_optional_access，Task 5b 发现）。**注册的头是 announced header
    /// （result.m_block->blockHeader()），不是 executed header**——finishExecute 只填承诺字段，
    /// executed 头的 tars encode 不全（缺 coinbase/gasLimit/baseFee/prevRandao/excessBlobGas/
    /// parentBeaconBlockRoot/...）；子块的 step 3a parent-header 读会经 createBlockHeader 重解析
    /// 该头，不全的头触发 dataHash 重算抛异常（Task 5b wiring 发现）。OP 头 hash 在 codec，
    /// 不调 BlockHeader::hash()（A8）。
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        ViewType& /*view*/, protocol::BlockHeader::Ptr /*header*/,
        bcos::scheduler_v1::SchedulerExecuteResult const& result)
    {
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(result.modeExtra);
        auto storage = std::make_shared<typename MultiLayerStorage::MutableStorage>();

        // The block's header is the announced one (the engine's buildOpBlock set it); finishExecute
        // built a separate executed header without touching the block's. Keep the Ptr alive:
        // blockHeader() returns a fresh shared_ptr BY VALUE, and binding a reference to
        // `*block->blockHeader()` would dangle a temporary (OpBlockScheduler's documented
        // segfault-on-timestamp() trap).
        auto announcedHeader = result.m_block->blockHeader();
        co_await bcos::evm::engine::opstackRegisterBlock(*storage, *announcedHeader,
            extra.announcedBlockHash, extra.rawTxBytes, extra.result, *this->m_blockFactory,
            m_envelopeToTars);
        co_return storage;
    }

    /// 测试观察面（Task 6 P1-8 harness 手术）：dual-path harness 经 executeBlock 驱动 execute
    /// hook 后需要拿回 OpExecuteBlockResult 做 A-vs-B 对比。返回最新 pending 结果里的原始执行
    /// 结果（骨架 pushResult 后 m_results.front() 即最新块）。生产路径不消费——commit hook 走
    /// result.modeExtra 的 OpExecuteExtra（字段更全，含 announcedBlockHash/rawTxBytes）。
    std::optional<bcos::evm::engine::OpExecuteBlockResult> peekExecuteResult()
    {
        std::unique_lock<std::mutex> lock(this->m_resultsMutex);
        if (this->m_results.empty())
            return std::nullopt;
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(this->m_results.front()->modeExtra);
        return extra.result;
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

    /// 骨架 catch(...) 兜底处的错误消息恢复（wiring Task 5b）：rethrow + typed catch——FISCO
    /// 类型 typeinfo 稳定，可靠绑定并取 what()（catch(std::exception&) 在 -fno-rtti evmone 边界
    /// 不可靠绑定，原消息会丢）。六字段 mismatch（verifyResult 抛的 OpConsensusError）与
    /// executeOpBlock 的 consensus/storage 拒绝消息都经此恢复，供引擎 barrier 生成带明细的
    /// validationError（-fno-rtti 边界问题详述见 SchedulerSkeleton.h describeException 注释）。
    std::string describeException(std::exception_ptr eptr) const override
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::engine::OpConsensusError& e)
        {
            return e.what();
        }
        catch (const bcos::evm::engine::OpStorageError& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unclassified exception, RTTI typed-catch bypassed";
        }
    }

    OpScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps,
        bcos::protocol::BlockFactory::Ptr blockFactory, MultiLayerStorage& multiLayerStorage,
        bcos::evm::engine::EnvelopeToTarsConverter envelopeToTars)
      : SchedulerBase(),
        m_schedulerImpl(receiptFactory, chainId, forkTimestamps),
        // 分叉键缓存：拷贝 Ptr（shared_ptr）后再 move 给 m_receiptFactory/m_hashImpl——缓存
        // 持有的拷贝独立存活。
        m_executorCache(receiptFactory, hashImpl),
        m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_envelopeToTars(std::move(envelopeToTars))
    {
        this->m_multiLayerStorage = &multiLayerStorage;
        this->m_blockFactory = blockFactory.get();
        // v3（M4 尾部）：OP commit 的 notifyBlockNumber 走骨架（m_asyncGroup），OP 无 RPC
        // 推送需求——默认注册 no-op notifier，composition root（Initializer）可经
        // setBlockNumberNotifier/setTransactionNotifier 覆盖。缺省为空 std::function 调用会抛
        // std::bad_function_call（异步任务内 → dtor 的 m_asyncGroup.wait() 重抛 → terminate）。
        // OP 的 m_transactions 恒为非空空表，tx-submit zip 空循环，m_transactionSubmitResultFactory
        // 为 null 也安全（createTxSubmitResult 从不被调）。
        this->m_blockNumberNotifier = [](bcos::protocol::BlockNumber) {};
        this->m_transactionNotifier =
            [](bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
                std::function<void(bcos::Error::Ptr)> cb) { cb(nullptr); };
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

    /// commit 连续性（v3 P1-6 头推进守卫）：opstackRegisterBlock 写 SYS_CURRENT_STATE 是无条件写
    /// （MAIN OpBlockRegister.h:75-80）——引擎原有守卫写（blockNumber >
    /// currentHead，EngineServiceImpl 内联路径）搬到 OpScheduler commit 后由本 override
    /// 承载，单调守卫语义保留（拒绝已提交 / 断连提交）。骨架 base 版读
    /// *m_ledger（getCurrentBlockNumber(*m_ledger)），OP composition root 不 wire
    /// LedgerInterface——这里改读 storage view（getCurrentBlockNumber(view, fromStorage)， 与引擎
    /// step 3 同源）。置 public 同 loadCommitLedgerConfig（coCommitBlock 经 derived() 访问）。
    /// isSysContractDeploy 特例保留（block 0 系统合约部署块，PrecompiledTypeDef.h:31）。
    task::Task<bool> commitContinuityCheck(protocol::BlockNumber number)
    {
        if (!isSysContractDeploy(number))
        {
            if (this->m_lastCommittedBlockNumber == -1)
            {
                auto view = this->m_multiLayerStorage->fork();
                this->m_lastCommittedBlockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
            }
            if (this->m_lastCommittedBlockNumber != -1 &&
                number <= this->m_lastCommittedBlockNumber)
            {
                BASELINE_SCHEDULER_LOG(INFO) << "Block already committed: " << number
                                             << "! latest: " << this->m_lastCommittedBlockNumber;
                co_return false;
            }
            if (this->m_lastCommittedBlockNumber != -1 &&
                number - this->m_lastCommittedBlockNumber != 1)
            {
                BASELINE_SCHEDULER_LOG(INFO)
                    << "Discontinuous commit block number: " << number
                    << "! expect: " << (this->m_lastCommittedBlockNumber + 1);
                co_return false;
            }
        }
        co_return true;
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

        // SEV-9：分叉键缓存取 executor（不每调现构造——OpBlockScheduler.h:298 先例的接线后形态）。
        auto& executor = m_executorCache.get(m_forkTimestamps, m_chainId, cfg);

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
    bcos::executor_v1::opstack::OpstackExecutorCache m_executorCache;
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
