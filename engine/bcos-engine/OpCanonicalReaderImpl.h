/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpCanonicalReaderImpl.h
 * @brief bcos::engine::OpCanonicalReader over a real OpEngineService + OpScheduler.
 */
#pragma once

#include "EngineServiceCommon.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/engine/OpCanonicalReader.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-storage/MPTNodeReadStorage.h>
#include <bcos-utilities/Common.h>
#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace bcos::engine
{

/// Forkchoice comes from @p EngineServiceT and branch views from @p DelegateT. Owns the
/// engine and delegate and shares the backend owner with the storage handles it returns.
template <class EngineServiceT, class DelegateT>
class OpCanonicalReaderImpl final : public OpCanonicalReader
{
public:
    using ViewType = typename DelegateT::ViewType;

    OpCanonicalReaderImpl(std::shared_ptr<const void> backendOwner,
        std::shared_ptr<EngineServiceT> engineService, std::shared_ptr<DelegateT> delegate,
        bcos::protocol::BlockFactory::Ptr blockFactory)
      : m_backendOwner(std::move(backendOwner)),
        m_engineService(std::move(engineService)),
        m_delegate(std::move(delegate)),
        m_blockFactory(std::move(blockFactory))
    {}

    task::Task<BlockRef> finalized() override
    {
        auto [number, hash] = co_await m_delegate->finalizedTip();
        co_return BlockRef{.hash = hash, .number = number};
    }

    task::Task<BlockRef> head() override
    {
        for (;;)
        {
            auto const tracked = m_engineService->trackedHead();
            if (tracked && m_delegate->unfinalizedBlock(tracked->hash))
            {
                co_return BlockRef{.hash = tracked->hash, .number = tracked->blockNumber};
            }
            // Sample the finalized tip after a window miss: the tracked block may have just
            // been merged. A replaced tracker head deserves its own lookup before we fall back.
            auto const fin = co_await finalized();
            auto const current = m_engineService->trackedHead();
            if (!current || (tracked && current->hash == tracked->hash))
            {
                co_return fin;
            }
        }
    }

    task::Task<protocol::BlockNumber> safeNumber() override
    {
        co_return (co_await captureHeadAndSafe()).second;
    }

    task::Task<HeadReadView> captureHeadView() override
    {
        auto [selected, safe] = co_await captureHeadAndSafe();
        co_return HeadReadView{.head = selected.number,
            .safe = safe,
            .finalized = selected.finalizedNumber,
            .window = std::make_unique<ReadView>(m_backendOwner, m_blockFactory,
                typename DelegateT::WindowBlockView{selected.number, std::move(selected.view)},
                selected.finalizedNumber)};
    }

    task::Task<std::optional<WindowBlockView>> captureWindowView(
        crypto::HashType const& hash) override
    {
        auto entry = co_await m_delegate->windowBlockView(hash);
        if (!entry)
        {
            co_return std::nullopt;
        }
        co_return WindowBlockView{.number = entry->number,
            .window =
                std::make_unique<ReadView>(m_backendOwner, m_blockFactory, std::move(*entry))};
    }

    task::Task<std::optional<crypto::HashType>> canonicalHashAt(
        protocol::BlockNumber number) override
    {
        auto tip = co_await head();
        while (number >= 0 && number <= tip.number && tip.hash != crypto::HashType{})
        {
            if (number == tip.number)
            {
                co_return tip.hash;
            }
            if (auto hash = co_await m_delegate->hashAtHeightOnChain(tip.hash, number))
            {
                co_return hash;
            }
            auto const current = co_await head();
            if (current.hash == tip.hash)
            {
                break;
            }
            tip = current;
        }
        co_return std::nullopt;
    }

    std::optional<protocol::BlockNumber> unfinalizedNumberOf(
        crypto::HashType const& blockHash) const override
    {
        auto entry = m_delegate->unfinalizedBlock(blockHash);
        return entry ? std::optional(entry->number) : std::nullopt;
    }

    task::Task<protocol::Block::Ptr> unfinalizedBlock(
        crypto::HashType const& blockHash, int32_t blockFlag) override
    {
        auto entry = co_await m_delegate->windowBlockLedger(blockHash);
        if (!entry)
        {
            co_return nullptr;
        }
        auto& [number, layer] = *entry;
        co_return co_await bcos::ledger::getBlockData(*layer, number, blockFlag, *m_blockFactory);
    }

    task::Task<std::optional<ChainTransaction>> transactionOnHeadChain(
        crypto::HashType const& txHash, int32_t blockFlag = 0) override
    {
        auto selected = co_await m_delegate->headViewWithRef();
        if (selected.hash == crypto::HashType{})
        {
            co_return std::nullopt;
        }
        auto const key = std::string(bcos::concepts::bytebuffer::toView(txHash));
        auto const keys =
            std::array{executor_v1::StateKeyView{bcos::ledger::SYS_HASH_2_RECEIPT, key},
                executor_v1::StateKeyView{bcos::ledger::SYS_HASH_2_TX, key}};
        // Keep the committed inclusion when a window block repeats the same deposit hash.
        auto rows = co_await storage2::readSome(selected.view.backendStorageRef(), keys);
        if ((!rows[0] || !rows[1]) && selected.number > selected.finalizedNumber)
        {
            rows = co_await storage2::readSome(selected.view, keys);
        }
        auto const& receiptEntry = rows[0];
        auto const& txEntry = rows[1];
        if (!receiptEntry || !txEntry)
        {
            co_return std::nullopt;
        }
        // Same decoders as getBlockDataFromStorages (LedgerMethods.h) — one row format.
        auto const receiptBytes = receiptEntry->get();
        auto receipt = m_blockFactory->receiptFactory()->createReceipt(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(receiptBytes.data()), receiptBytes.size()));
        auto const txBytes = txEntry->get();
        auto transaction = m_blockFactory->transactionFactory()->createTransaction(
            bcos::bytesConstRef(
                reinterpret_cast<const bcos::byte*>(txBytes.data()), txBytes.size()),
            false, false, false);
        auto const number = receipt->blockNumber();
        if (number < 0 || number > selected.number)
        {
            co_return std::nullopt;
        }
        auto blockHash =
            co_await bcos::ledger::getBlockHash(selected.view, number, bcos::ledger::fromStorage);
        if (!blockHash)
        {
            co_return std::nullopt;
        }
        protocol::Block::Ptr block;
        if (blockFlag != 0)
        {
            if (number > selected.finalizedNumber)
            {
                typename DelegateT::WindowBlockView window{
                    selected.number, std::move(selected.view)};
                block = co_await bcos::ledger::getBlockData(
                    window.ledgerAt(number), number, blockFlag, *m_blockFactory);
            }
            else
            {
                // A later window block can repeat a deposit hash from the committed block.
                // Decode committed rows through the same backend snapshot without those layers.
                selected.view.m_immutableStorages.clear();
                block = co_await bcos::ledger::getBlockData(
                    selected.view, number, blockFlag, *m_blockFactory);
            }
        }
        co_return ChainTransaction{.transaction = std::move(transaction),
            .receipt = std::move(receipt),
            .blockHash = *blockHash,
            .block = std::move(block)};
    }

    task::Task<std::shared_ptr<MPTNodeReader>> mptNodeReaderAt(
        crypto::HashType const& blockHash) override
    {
        auto view = co_await m_delegate->mptViewAt(blockHash);
        if (!view)
        {
            co_return nullptr;
        }
        co_return storage2::makeMPTNodeReader(std::move(*view), m_backendOwner);
    }

    task::Task<std::shared_ptr<StateStorage>> stateStorageAt(
        crypto::HashType const& blockHash) override
    {
        auto view = co_await m_delegate->viewAt(blockHash);
        if (!view)
        {
            co_return nullptr;
        }
        co_return ownStateStorage(std::move(*view));
    }

    task::Task<std::shared_ptr<StateStorage>> headStateStorage() override
    {
        auto selected = co_await m_delegate->headViewWithRef();
        if (selected.hash == crypto::HashType{})
        {
            co_return nullptr;
        }
        co_return ownStateStorage(std::move(selected.view));
    }

private:
    task::Task<std::pair<typename DelegateT::HeadView, protocol::BlockNumber>> captureHeadAndSafe()
    {
        for (;;)
        {
            auto const [tracked, safe] = m_engineService->trackedHeadAndSafe();
            auto selected = co_await m_delegate->headViewWithRef(
                tracked ? std::optional(tracked->hash) : std::nullopt);
            if (tracked && tracked->hash != selected.hash)
            {
                // The sampled head was pruned before capture. Retry only if a new tracker's
                // head is available; a stale tracker after a partial finalize uses the disk tip.
                auto const current = m_engineService->trackedHead();
                if (current && current->hash != tracked->hash)
                {
                    continue;
                }
            }
            auto safeNumber = selected.finalizedNumber;
            if (safe && safe->blockNumber > safeNumber && safe->blockNumber <= selected.number &&
                (co_await ledger::getBlockHash(
                    selected.view, safe->blockNumber, ledger::fromStorage)) == safe->hash)
            {
                safeNumber = safe->blockNumber;
            }
            co_return {std::move(selected), safeNumber};
        }
    }

    class ReadView final : public WindowReadView
    {
    public:
        ReadView(std::shared_ptr<const void> backendOwner, protocol::BlockFactory::Ptr blockFactory,
            typename DelegateT::WindowBlockView window, protocol::BlockNumber finalizedNumber = -1)
          : m_backendOwner(std::move(backendOwner)),
            m_blockFactory(std::move(blockFactory)),
            m_window(std::move(window)),
            m_finalizedNumber(finalizedNumber)
        {}

        task::Task<protocol::Block::Ptr> block(protocol::BlockNumber number, int32_t flags) override
        {
            if (number <= m_finalizedNumber)
            {
                return bcos::ledger::getBlockData(
                    m_window.view.backendStorageRef(), number, flags, *m_blockFactory);
            }
            // All block rows live in its own ledger layer. A descendant that repeats a deposit
            // hash cannot shadow this block's receipt. Archival only applies below the window.
            return bcos::ledger::getBlockData(
                m_window.ledgerAt(number), number, flags, *m_blockFactory);
        }

        task::Task<std::optional<protocol::BlockNumber>> blockNumber(crypto::HashType hash) override
        {
            return bcos::ledger::getBlockNumber(m_window.view, hash, bcos::ledger::fromStorage);
        }

        std::shared_ptr<MPTNodeReader> takeMptNodeReader(
            std::shared_ptr<StateStorage>* state) override
        {
            if (state)
            {
                *state = takeStateStorage();
                return storage2::makeMPTNodeReader(**state, *state);
            }
            return storage2::makeMPTNodeReader(std::move(m_window.view), std::move(m_backendOwner));
        }

        std::shared_ptr<StateStorage> takeStateStorage() override
        {
            return storage2::makeOwningAnyStorage<executor_v1::StateKey, executor_v1::StateValue>(
                std::move(m_window.view), std::move(m_backendOwner));
        }

    private:
        std::shared_ptr<const void> m_backendOwner;
        protocol::BlockFactory::Ptr m_blockFactory;
        typename DelegateT::WindowBlockView m_window;
        protocol::BlockNumber m_finalizedNumber;
    };

    std::shared_ptr<StateStorage> ownStateStorage(ViewType&& view) const
    {
        return storage2::makeOwningAnyStorage<executor_v1::StateKey, executor_v1::StateValue>(
            std::move(view), m_backendOwner);
    }

    std::shared_ptr<const void> m_backendOwner;
    std::shared_ptr<EngineServiceT> m_engineService;
    std::shared_ptr<DelegateT> m_delegate;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};

}  // namespace bcos::engine
