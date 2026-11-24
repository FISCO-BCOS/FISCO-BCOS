// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-transaction-scheduler/HistoricalCallStorage.h>
#include <bcos-tx-validator/TxValidator.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <opstack-executor/OpAdmissionRollupCost.h>
#include <opstack-executor/OpForkSpec.h>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace bcos::initializer
{
/// Keep the OP admission snapshot separate from the node's finalized LedgerConfigState.
/// A request uses one canonical chain view, and reads both the sender and L1Block at its
/// header's state root even if forkchoice or finality changes during the reads.
template <class Scheduler, class StorageOwner>
txvalidator::AdmissionChainStateFn makeOpAdmissionChainState(std::weak_ptr<Scheduler> scheduler,
    std::shared_ptr<StorageOwner> storageOwner, protocol::BlockFactory::Ptr blockFactory,
    ledger::OpForkSchedule schedule)
{
    namespace op = executor_v1::opstack;
    struct HeadCache
    {
        std::mutex mutex;
        std::shared_ptr<const ledger::LedgerConfig> config;
        op::OpAdmissionRollupCost rollupCost;
    };
    auto cache = std::make_shared<HeadCache>();
    return [scheduler = std::move(scheduler), storageOwner = std::move(storageOwner),
               blockFactory = std::move(blockFactory), schedule,
               cache = std::move(cache)](protocol::Transaction const& tx,
               txvalidator::Check checks) -> task::Task<txvalidator::AdmissionChainState> {
        // The scheduler's view borrows the backend; keep its owner in the callable.
        (void)storageOwner;
        auto delegate = scheduler.lock();
        if (!delegate)
        {
            throw std::runtime_error("OP admission: scheduler unavailable");
        }
        auto view = co_await delegate->headView();
        auto const number = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
        auto const headHash = co_await ledger::getBlockHash(view, number, ledger::fromStorage);
        auto block = co_await ledger::getBlockData(view, number, ledger::HEADER, *blockFactory);
        auto header = block ? block->blockHeader() : nullptr;
        if (!headHash || !header)
        {
            throw std::runtime_error("OP admission: canonical head header unavailable");
        }
        auto const& hash = *headHash;
        auto const spec = op::opForkSpecAt(schedule, op::opForkTimestampSec(header->timestamp()));

        txvalidator::AdmissionChainState result{};
        result.revision = spec.rev;
        {
            std::lock_guard lock(cache->mutex);
            if (cache->config && cache->config->hash() == hash)
            {
                result.config = cache->config;
            }
        }
        if (!result.config)
        {
            auto config = std::make_shared<ledger::LedgerConfig>();
            auto nodes = co_await ledger::getNodeList(view);
            ledger::SystemConfigs systemConfigs;
            // The admission checks judge a transaction for the block following the head.
            co_await ledger::readFromStorage(systemConfigs, view, number + 1);
            ledger::applyLedgerConfig(*config, nodes, systemConfigs, {}, number,
                header->timestamp(), hash, protocol::blockBaseFee(*header));
            std::lock_guard lock(cache->mutex);
            cache->config = config;
            result.config = std::move(config);
        }
        if ((checks & txvalidator::c_accountStateDependent) == txvalidator::Check::None)
        {
            co_return result;
        }

        using View = decltype(view);
        using HistoricalBackend = scheduler_v1::HistoricalStateBackend<View>;
        HistoricalBackend backend(view, header->stateRoot(),
            ledger::account::nodeAddressTableMode(), /*ethLaneNaming=*/true);
        storage2::View<typename View::MutableStorage, void, HistoricalBackend> rootedView(
            std::addressof(backend));
        ledger::account::EVMAccount account(rootedView, ledger::account::FromTableName{},
            ledger::account::legacyAppsAccountTableName(toHex(tx.sender())));
        auto& sender = result.sender.emplace();
        sender.balance = co_await account.balance();
        if (auto nonce = co_await account.nonce())
        {
            sender.nonce = u256(*nonce);
        }
        if ((checks & txvalidator::c_rollupCostDependent) != txvalidator::Check::None)
        {
            auto const envelope = bcostars::protocol::reassembleWeb3RawTransaction(
                tx.extraTransactionBytes(), tx.signatureData());
            result.rollupCost = co_await cache->rollupCost.price(
                rootedView, hash, spec, ref(envelope), static_cast<uint64_t>(tx.gasLimit()));
        }
        co_return result;
    };
}
}  // namespace bcos::initializer
