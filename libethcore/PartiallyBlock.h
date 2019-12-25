/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief block for bip-0152
 * @author: yujiechen
 * @date: 2019-11-12
 */
#pragma once
#include "Block.h"

#define PartiallyBlock_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("PartiallyBlock")

namespace dev
{
namespace eth
{
class PartiallyBlock : public Block
{
public:
    using Ptr = std::shared_ptr<PartiallyBlock>;
    PartiallyBlock() : Block()
    {
        m_txsHash = std::make_shared<std::vector<dev::h256>>();
        m_missedTxs = std::make_shared<std::vector<std::pair<dev::h256, uint64_t>>>();
        m_missedTransactions = std::make_shared<Transactions>();
    }

    PartiallyBlock(std::shared_ptr<PartiallyBlock> _block) : Block(*_block)
    {
        m_txsHash = std::make_shared<std::vector<dev::h256>>(*(_block->txsHash()));
        m_missedTxs =
            std::make_shared<std::vector<std::pair<dev::h256, uint64_t>>>(*(_block->missedTxs()));
        m_missedTransactions = std::make_shared<Transactions>(*(_block->missedTransactions()));
    }

    ~PartiallyBlock() override {}
    void encodeProposal(std::shared_ptr<bytes> _out, bool const& _onlyTxsHash = false) override;

    void decodeProposal(bytesConstRef _blockBytes, bool const& _onlyTxsHash = false) override;

    // fillBlock with fetched transaction
    void fillBlock(bytesConstRef _txsData);
    bool txsAllHit() override { return m_missedTxs->size() == 0; }

    // response encodeded-transactions when receive missed-txsRequest
    void fetchMissedTxs(std::shared_ptr<bytes> _encodedBytes, bytesConstRef _missInfo,
        dev::h256 const& _expectedHash);
    // encode the missed info
    void encodeMissedInfo(std::shared_ptr<bytes> _encodedBytes);

    std::shared_ptr<std::vector<std::pair<dev::h256, uint64_t>>> missedTxs() { return m_missedTxs; }

    std::shared_ptr<std::vector<dev::h256>> txsHash() { return m_txsHash; }
    std::shared_ptr<Transactions> missedTransactions() { return m_missedTransactions; }

private:
    void encodeMissedTxs(std::shared_ptr<bytes> _encodedBytes,
        std::shared_ptr<std::vector<std::pair<dev::h256, uint64_t>>> _missedTxs,
        dev::h256 const& _expectedHash);
    void calTxsHashBytes();
    void checkBasic(RLP const& rlp, dev::h256 const& _expectedHash);

private:
    // record the transaction hash
    std::shared_ptr<std::vector<dev::h256>> m_txsHash;
    // record the missed transactions
    std::shared_ptr<std::vector<std::pair<dev::h256, uint64_t>>> m_missedTxs;
    std::shared_ptr<Transactions> m_missedTransactions;
};
}  // namespace eth
}  // namespace dev