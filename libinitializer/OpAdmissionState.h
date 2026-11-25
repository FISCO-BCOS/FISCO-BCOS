// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <bcos-framework/ledger/Ledger.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/ViewNodeStorage.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-tx-validator/TxValidator.h>
#include <opstack-executor/OpAdmissionRollupCost.h>
#include <opstack-executor/OpForkSpec.h>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace bcos::initializer
{
/// Keep the OP admission snapshot separate from the node's finalized LedgerConfigState.
/// A request uses one canonical chain view. The sender leaf is read at that head's state root;
/// L1Block fee slots come from the same pinned view used by block execution.
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
            if (!header || protocol::canonicalBlockHash(*header) != hash)
            {
                throw std::runtime_error(
                    "OP admission: canonical head header does not match its hash");
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

        auto& sender = result.sender.emplace();
        ledger::mpt::ViewNodeStorage nodes(view);
        ledger::mpt::MPTReadView state(nodes, head->stateRoot);
        auto const address = Address{tx.sender(), Address::FromBinary, Address::AcquireEqual};
        if (auto account = co_await state.readAccount(address))
        {
            sender.balance = account->balance;
            sender.nonce = account->nonce;
        }
        if ((checks & txvalidator::c_rollupCostDependent) != txvalidator::Check::None)
        {
            auto const envelope = bcostars::protocol::reassembleWeb3RawTransaction(
                tx.extraTransactionBytes(), tx.signatureData());
            result.rollupCost = co_await cache->rollupCost.price(
                view, hash, head->spec, ref(envelope), static_cast<uint64_t>(tx.gasLimit()));
        }
        co_return result;
    };
}
}  // namespace bcos::initializer
