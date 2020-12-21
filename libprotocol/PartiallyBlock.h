/*
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief block for bip-0152
 * @author: yujiechen
 * @date: 2019-11-12
 */
#pragma once
#include "Block.h"

#define PartiallyBlock_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("PartiallyBlock")

namespace bcos
{
namespace protocol
{
class PartiallyBlock : public Block
{
public:
    using Ptr = std::shared_ptr<PartiallyBlock>;
    PartiallyBlock() : Block()
    {
        m_txsHash = std::make_shared<std::vector<bcos::h256>>();
        m_missedTxs = std::make_shared<std::vector<std::pair<bcos::h256, uint64_t>>>();
        m_missedTransactions = std::make_shared<Transactions>();
    }

    PartiallyBlock(std::shared_ptr<PartiallyBlock> _block) : Block(*_block)
    {
        m_txsHash = std::make_shared<std::vector<bcos::h256>>(*(_block->txsHash()));
        m_missedTxs =
            std::make_shared<std::vector<std::pair<bcos::h256, uint64_t>>>(*(_block->missedTxs()));
        m_missedTransactions = std::make_shared<Transactions>(*(_block->missedTransactions()));
    }

    ~PartiallyBlock() override {}
    void encodeProposal(std::shared_ptr<bytes> _out, bool const& _onlyTxsHash = false) override;

    void decodeProposal(bytesConstRef _blockBytes, bool const& _onlyTxsHash = false) override;

    // fillBlock with fetched transaction
    void fillBlock(bytesConstRef _txsData);
    void fillBlock(RLP const& _rlp);
    bool txsAllHit() override { return m_missedTxs->size() == 0; }

    // response encodeded-transactions when receive missed-txsRequest
    void fetchMissedTxs(std::shared_ptr<bytes> _encodedBytes, bytesConstRef _missInfo,
        bcos::h256 const& _expectedHash);
    // encode the missed info
    void encodeMissedInfo(std::shared_ptr<bytes> _encodedBytes);

    std::shared_ptr<std::vector<std::pair<bcos::h256, uint64_t>>> missedTxs()
    {
        return m_missedTxs;
    }

    std::shared_ptr<std::vector<bcos::h256>> txsHash() { return m_txsHash; }
    std::shared_ptr<Transactions> missedTransactions() { return m_missedTransactions; }

private:
    void encodeMissedTxs(std::shared_ptr<bytes> _encodedBytes,
        std::shared_ptr<std::vector<std::pair<bcos::h256, uint64_t>>> _missedTxs,
        bcos::h256 const& _expectedHash);
    void calTxsHashBytes();
    void checkBasic(RLP const& rlp, bcos::h256 const& _expectedHash);

private:
    // record the transaction hash
    std::shared_ptr<std::vector<bcos::h256>> m_txsHash;
    // record the missed transactions
    std::shared_ptr<std::vector<std::pair<bcos::h256, uint64_t>>> m_missedTxs;
    std::shared_ptr<Transactions> m_missedTransactions;
};
}  // namespace protocol
}  // namespace bcos
