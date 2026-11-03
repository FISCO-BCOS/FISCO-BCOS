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
 * @file LedgerBlockSource.h
 * @brief The ledger-backed BlockSourceInterface the devp2p serving loop (PeerServer)
 *        answers inbound peers from on the Ethereum L1 EL lane: canonical headers,
 *        bodies (wire transactions reassembled from the stored signing payload +
 *        signature, withdrawals from the SYS_NUMBER_2_WITHDRAWALS sidecar) and
 *        receipts (toEthReceiptData, typed by the transaction at the same index) —
 *        the same reads engine_getPayloadBodiesBy* and the receipts-root computation
 *        already perform, reused here instead of a second encoder.
 * @date 2026/11/3
 */
#pragma once

#include "bcos-devp2p/eth/Protocol.h"
#include "bcos-devp2p/sync/BlockSource.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthReceipt.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"
#include "bcos-task/Wait.h"
#include "libinitializer/Common.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::initializer
{
class LedgerBlockSource : public bcos::devp2p::sync::BlockSourceInterface
{
public:
    // _genesisHeader: the chain genesis in the Ethereum header domain ([eth_genesis_header]
    // projection) — served byte-exact for block 0 and the head identity of a fresh node.
    LedgerBlockSource(bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::protocol::EthBlockHeaderData _genesisHeader)
      : m_ledger(std::move(_ledger)),
        m_genesisHeader(std::move(_genesisHeader)),
        m_genesisHash(bcos::protocol::ethHeaderHash(m_genesisHeader))
    {
        bcos::codec::rlp::encode(m_genesisRlp, m_genesisHeader);
    }

    bcos::devp2p::sync::BlockRange range() override
    {
        auto const head = currentHead();
        if (head <= 0)
        {
            return {0, 0, m_genesisHash};
        }
        auto header = ethHeader(head);
        if (!header)
        {
            return {0, 0, m_genesisHash};
        }
        return {0, static_cast<uint64_t>(head), bcos::protocol::ethHeaderHash(*header)};
    }

    std::optional<uint64_t> numberByHash(bcos::h256 const& _hash) override
    {
        if (_hash == m_genesisHash)
        {
            return 0;
        }
        // The EL lane indexes blocks by their Ethereum hash (the same lookup
        // engine_getPayloadBodiesByHashV1 relies on); a lookup fault reads as unknown.
        try
        {
            auto const number = task::syncWait(ledger::getBlockNumber(*m_ledger, _hash));
            if (number < 0 || number > currentHead())
            {
                return std::nullopt;
            }
            return static_cast<uint64_t>(number);
        }
        catch (std::exception const&)
        {
            return std::nullopt;
        }
    }

    std::optional<bcos::bytes> headerRlp(uint64_t _number) override
    {
        if (_number == 0)
        {
            return m_genesisRlp;
        }
        auto header = ethHeader(static_cast<protocol::BlockNumber>(_number));
        if (!header)
        {
            return std::nullopt;
        }
        bcos::bytes out;
        bcos::codec::rlp::encode(out, *header);
        return out;
    }

    std::optional<bcos::devp2p::eth::BlockBody> body(uint64_t _number) override
    {
        if (_number == 0)
        {
            // Genesis has no transactions; Shanghai-at-genesis chains carry an empty
            // withdrawals list (withdrawalsHash present), pre-Shanghai ones none.
            bcos::devp2p::eth::BlockBody body;
            if (m_genesisHeader.withdrawalsHash.has_value())
            {
                body.withdrawals = std::vector<bcos::bytes>{};
            }
            return body;
        }
        auto const number = static_cast<protocol::BlockNumber>(_number);
        if (number > currentHead())
        {
            return std::nullopt;
        }
        try
        {
            auto block = task::syncWait(
                ledger::getBlockData(*m_ledger, number, ledger::HEADER | ledger::TRANSACTIONS));
            if (!block || !block->blockHeader())
            {
                return std::nullopt;
            }
            bcos::devp2p::eth::BlockBody body;
            for (auto const& tx : block->transactions())
            {
                // A committed EL block's transactions are all Web3 (EIP-2718): the signing
                // payload (extraTransactionBytes) plus the signature reassembles into the
                // exact wire bytes the block was committed with; an OP deposit (0x7e,
                // unsigned) is stored as its full envelope.
                if (tx->type() != static_cast<uint8_t>(protocol::TransactionType::Web3Transaction))
                {
                    INITIALIZER_LOG(WARNING)
                        << LOG_DESC("peer serving: block carries a non-EIP-2718 transaction")
                        << LOG_KV("number", _number);
                    return std::nullopt;
                }
                if (tx->isDepositTx())
                {
                    body.transactions.push_back(tx->extraTransactionBytes().toBytes());
                    continue;
                }
                body.transactions.push_back(bcostars::protocol::reassembleWeb3RawTransaction(
                    tx->extraTransactionBytes(), tx->signatureData()));
            }
            // Withdrawals: the sidecar row the EL verifier's commit writes for every
            // Shanghai+ block (an RLP list of withdrawal items). A Shanghai+ block
            // without the row cannot be served (the peer's body hash check would fail).
            if (block->blockHeader()->withdrawalsRoot().has_value())
            {
                auto const stateStorage = m_ledger->getStateStorage();
                if (!stateStorage)
                {
                    return std::nullopt;
                }
                auto const entry = task::syncWait(storage2::readOne(
                    *stateStorage, executor_v1::StateKeyView{
                                       ledger::SYS_NUMBER_2_WITHDRAWALS, std::to_string(_number)}));
                if (!entry.has_value())
                {
                    return std::nullopt;
                }
                auto const raw = entry->get();
                body.withdrawals = splitWithdrawals(bcos::bytes(raw.begin(), raw.end()));
                if (!body.withdrawals)
                {
                    return std::nullopt;
                }
            }
            return body;
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING) << LOG_DESC("peer serving: cannot read block body")
                                     << LOG_KV("number", _number) << LOG_KV("error", e.what());
            return std::nullopt;
        }
    }

    std::optional<std::vector<bcos::protocol::EthReceiptData>> receipts(uint64_t _number) override
    {
        if (_number == 0)
        {
            return std::vector<bcos::protocol::EthReceiptData>{};
        }
        auto const number = static_cast<protocol::BlockNumber>(_number);
        if (number > currentHead())
        {
            return std::nullopt;
        }
        try
        {
            auto block = task::syncWait(
                ledger::getBlockData(*m_ledger, number, ledger::TRANSACTIONS | ledger::RECEIPTS));
            if (!block)
            {
                return std::nullopt;
            }
            auto receipts = block->receipts();
            std::vector<bcos::protocol::EthReceiptData> out;
            out.reserve(receipts.size());
            auto receipt = receipts.begin();
            for (auto const& tx : block->transactions())
            {
                if (receipt == receipts.end())
                {
                    return std::nullopt;  // receipt/transaction count mismatch
                }
                bcos::protocol::EthReceiptData eth;
                bcos::protocol::toEthReceiptData(**receipt, tx->web3TypedTxKind(), eth);
                out.push_back(std::move(eth));
                ++receipt;
            }
            if (receipt != receipts.end())
            {
                return std::nullopt;
            }
            return out;
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING) << LOG_DESC("peer serving: cannot read block receipts")
                                     << LOG_KV("number", _number) << LOG_KV("error", e.what());
            return std::nullopt;
        }
    }

private:
    protocol::BlockNumber currentHead() const
    {
        try
        {
            return task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
        }
        catch (std::exception const&)
        {
            return -1;
        }
    }

    // The committed header converted back to the Ethereum header domain (the stored Tars
    // header re-encodes to the byte-exact committed RLP — the same invariant the sync
    // loop's resume anchor relies on).
    std::optional<bcos::protocol::EthBlockHeaderData> ethHeader(protocol::BlockNumber _number)
    {
        try
        {
            auto block = task::syncWait(ledger::getBlockData(*m_ledger, _number, ledger::HEADER));
            if (!block || !block->blockHeader())
            {
                return std::nullopt;
            }
            return bcos::protocol::EthBlockHeader(*block->blockHeader()).data();
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING) << LOG_DESC("peer serving: cannot read block header")
                                     << LOG_KV("number", _number) << LOG_KV("error", e.what());
            return std::nullopt;
        }
    }

    // Split the sidecar's RLP list into its complete withdrawal items (the BlockBodies
    // codec splices them as-is); nullopt when the row is not a closed list.
    static std::optional<std::vector<bcos::bytes>> splitWithdrawals(bcos::bytes _raw)
    {
        std::vector<bcos::bytes> items;
        try
        {
            auto view = bcos::ref(_raw);
            auto const listHeader = bcos::codec::rlp::decodeHeader(view);
            if (!listHeader.isList || listHeader.payloadLength != view.size())
            {
                return std::nullopt;
            }
            while (!view.empty())
            {
                auto const itemStart = view;
                auto const itemHeader = bcos::codec::rlp::decodeHeader(view);
                auto const itemSize = (itemStart.size() - view.size()) + itemHeader.payloadLength;
                items.emplace_back(itemStart.data(), itemStart.data() + itemSize);
                view = view.getCroppedData(itemHeader.payloadLength);
            }
        }
        catch (std::exception const&)
        {
            return std::nullopt;
        }
        return items;
    }

    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::protocol::EthBlockHeaderData m_genesisHeader;
    bcos::h256 m_genesisHash;
    bcos::bytes m_genesisRlp;
};
}  // namespace bcos::initializer
