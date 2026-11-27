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
#include <optional>
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
    struct HeadData
    {
        std::shared_ptr<const ledger::LedgerConfig> config;
        crypto::HashType stateRoot;
        op::OpForkSpec spec;
    };
    struct HeadCache
    {
        std::mutex mutex;
        std::optional<HeadData> head;
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
        auto selected = co_await delegate->headViewWithRef();
        auto const number = selected.number;
        auto const& hash = selected.hash;
        auto view = std::move(selected.view);
        if (number < 0 || hash == crypto::HashType{})
        {
            throw std::runtime_error("OP admission: canonical head header unavailable");
        }

        std::optional<HeadData> head;
        {
            std::lock_guard lock(cache->mutex);
            if (cache->head && cache->head->config->hash() == hash)
            {
                head = cache->head;
            }
        }
        if (!head)
        {
            auto block = co_await ledger::getBlockData(view, number, ledger::HEADER, *blockFactory);
            auto header = block ? block->blockHeader() : nullptr;
            if (!header)
            {
                throw std::runtime_error("OP admission: canonical head header unavailable");
            }
            auto config = std::make_shared<ledger::LedgerConfig>();
            auto nodes = co_await ledger::getNodeList(view);
            ledger::SystemConfigs systemConfigs;
            // The admission checks judge a transaction for the block following the head.
            co_await ledger::readFromStorage(systemConfigs, view, number + 1);
            ledger::applyLedgerConfig(*config, nodes, systemConfigs, {}, number,
                header->timestamp(), hash, protocol::blockBaseFee(*header));
            head.emplace(HeadData{std::move(config), header->stateRoot(),
                op::opForkSpecAt(schedule, op::opForkTimestampSec(header->timestamp()))});
            std::lock_guard lock(cache->mutex);
            cache->head = head;
        }
        txvalidator::AdmissionChainState result{};
        result.config = std::move(head->config);
        result.revision = head->spec.rev;
        if ((checks & txvalidator::c_accountStateDependent) == txvalidator::Check::None)
        {
            co_return result;
        }

        using View = decltype(view);
        using HistoricalBackend = scheduler_v1::HistoricalStateBackend<View>;
        HistoricalBackend backend(
            view, head->stateRoot, ledger::account::nodeAddressTableMode(), /*ethLaneNaming=*/true);
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
                rootedView, hash, head->spec, ref(envelope), static_cast<uint64_t>(tx.gasLimit()));
        }
        co_return result;
    };
}
}  // namespace bcos::initializer
