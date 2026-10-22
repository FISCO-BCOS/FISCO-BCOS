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

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/engine/OpCanonicalReader.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-storage/MPTNodeReadStorage.h>
#include <bcos-utilities/Common.h>
#include <memory>
#include <optional>
#include <utility>

namespace bcos::engine
{

/// D1 §10.2 rules, in code: heights from @p EngineServiceT (the tracker: trackedHead /
/// getSafeBlockNumber), views from @p DelegateT (OpScheduler<MLS>: viewAt /
/// hashAtHeightOnChain / unfinalizedBlock / finalizedTip). Holds the engine service by
/// reference (owned by the same composition root, EngineServiceInitializer's holder) and the
/// delegate by shared_ptr.
template <class EngineServiceT, class DelegateT>
class OpCanonicalReaderImpl final : public OpCanonicalReader
{
public:
    using ViewType = typename DelegateT::ViewType;

    OpCanonicalReaderImpl(std::shared_ptr<EngineServiceT> engineService,
        std::shared_ptr<DelegateT> delegate, bcos::protocol::BlockFactory::Ptr blockFactory)
      : m_engineService(std::move(engineService)),
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
        if (auto tracked = m_engineService->trackedHead())
        {
            co_return BlockRef{.hash = tracked->hash, .number = tracked->blockNumber};
        }
        co_return co_await finalized();
    }

    task::Task<protocol::BlockNumber> safeNumber() override
    {
        if (auto safe = m_engineService->getSafeBlockNumber())
        {
            co_return *safe;
        }
        co_return (co_await finalized()).number;
    }

    task::Task<std::optional<crypto::HashType>> canonicalHashAt(
        protocol::BlockNumber number) override
    {
        auto const tip = co_await head();
        co_return co_await m_delegate->hashAtHeightOnChain(tip.hash, number);
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
        auto entry = m_delegate->unfinalizedBlock(blockHash);
        if (!entry)
        {
            co_return nullptr;
        }
        auto view = co_await m_delegate->viewAt(blockHash);
        if (!view)
        {
            // Finalized between the two lookups: the ledger owns it now.
            co_return nullptr;
        }
        // The block's own chain view: NUMBER_2_HASH[number] on it IS this block, so the
        // ledger-shaped decoder answers this branch's rows, not a sibling's.
        co_return co_await bcos::ledger::getBlockData(
            *view, entry->number, blockFlag, *m_blockFactory);
    }

    task::Task<std::optional<ChainTransaction>> transactionOnHeadChain(
        crypto::HashType const& txHash) override
    {
        auto const tip = co_await head();
        auto view = co_await m_delegate->viewAt(tip.hash);
        if (!view)
        {
            co_return std::nullopt;
        }
        auto const key = std::string(bcos::concepts::bytebuffer::toView(txHash));
        auto receiptEntry = co_await storage2::readOne(
            *view, executor_v1::StateKeyView{bcos::ledger::SYS_HASH_2_RECEIPT, key});
        auto txEntry = co_await storage2::readOne(
            *view, executor_v1::StateKeyView{bcos::ledger::SYS_HASH_2_TX, key});
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
        auto blockHash = co_await m_delegate->hashAtHeightOnChain(tip.hash, receipt->blockNumber());
        if (!blockHash)
        {
            co_return std::nullopt;
        }
        co_return ChainTransaction{.transaction = std::move(transaction),
            .receipt = std::move(receipt),
            .blockHash = *blockHash};
    }

    task::Task<std::shared_ptr<MPTNodeReader>> mptNodeReaderAt(
        crypto::HashType const& blockHash) override
    {
        auto view = co_await m_delegate->viewAt(blockHash);
        if (!view)
        {
            co_return nullptr;
        }
        co_return storage2::makeOwningMPTNodeReader(std::move(*view));
    }

    task::Task<std::shared_ptr<StateStorage>> stateStorageAt(
        crypto::HashType const& blockHash) override
    {
        auto view = co_await m_delegate->viewAt(blockHash);
        if (!view)
        {
            co_return nullptr;
        }
        co_return storage2::makeOwningAnyStorage<executor_v1::StateKey, executor_v1::StateValue>(
            std::move(*view));
    }

private:
    std::shared_ptr<EngineServiceT> m_engineService;
    std::shared_ptr<DelegateT> m_delegate;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};

}  // namespace bcos::engine
