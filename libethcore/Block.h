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
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief basic data structure for block
 *
 * @file Block.h
 * @author: yujiechem
 * @date 2018-09-20
 */
#pragma once
#include "BlockHeader.h"
#include "Transaction.h"
#include <libdevcore/Common.h>

namespace dev
{
namespace eth
{
class Block
{
public:
    ///-----constructors of Block
    Block() = default;
    explicit Block(bytesConstRef _data);
    explicit Block(bytes const& _data);
    /// copy constructor
    Block(Block const& _block);
    /// assignment operator
    Block& operator=(Block const& _block);

    ///-----opearator overloads of Block
    /// operator ==
    bool operator==(Block const& _block) const
    {
        return m_blockHeader == _block.blockHeader() && m_headerHash == _block.headerHash() &&
               m_sigList == _block.sigList() && m_transactions == _block.transactions() &&
               m_transactionHashSet == _block.transactionHashSet();
    }
    /// operator !=
    bool operator!=(Block const& _cmp) const { return !operator==(_cmp); }
    explicit operator bool() const { return bool(m_blockHeader) && (m_transactions.size() != 0); }

    ///-----encode functions
    void encode(
        bytes& _out, bytesConstRef _header, std::vector<std::pair<u256, Signature>>& sig_list);
    void encode(bytes& _out, bytesConstRef _header)
    {
        std::vector<std::pair<u256, Signature>> sig_list;
        encode(_out, _header, sig_list);
    }
    void encode(bytes& _out, std::vector<std::pair<u256, Signature>>& sig_list);
    void encode(bytes& _out)
    {
        std::vector<std::pair<u256, Signature>> sig_list;
        encode(_out, sig_list);
    }

    ///-----decode functions
    void decode(bytesConstRef _block);

    ///-----get interfaces
    Transactions const& transactions() const { return m_transactions; }
    Transaction const& transaction(bool& exist, size_t const _index) const
    {
        exist = false;
        if (m_transactions.size() > _index)
        {
            exist = true;
            return m_transactions[_index];
        }
        return Transaction();
    }
    BlockHeader const& blockHeader() const { return m_blockHeader; }
    h256 const& headerHash() const { return m_headerHash; }
    std::vector<std::pair<u256, Signature>> const& sigList() const { return m_sigList; }
    h256Hash const& transactionHashSet() const { return m_transactionHashSet; }

    ///-----set interfaces
    /// set m_transactions
    void setTransactions(Transactions const& _trans)
    {
        m_transactions = _trans;
        noteChange();
    }
    /// append a single transaction to m_transactions
    void appendTransaction(Transaction const& _trans) {}
    void setBlockHeader(BlockHeader const& _blockHeader) { m_blockHeader = _blockHeader; }

private:
    /// encode function
    inline void encode(bytes& _out, bytesConstRef block_header, h256 const& hash,
        std::vector<std::pair<u256, Signature>>& sig_list);
    /// callback this function when transaction has changed
    void noteChange() { m_transaction_changed = true; }
    void inline encodeTransactions();

private:
    /// block header of the block (field 0)
    BlockHeader m_blockHeader;
    /// transaction list (field 1)
    Transactions m_transactions;
    /// hash of the block header (field 2)
    h256 m_headerHash;
    /// sig list (field 3)
    std::vector<std::pair<u256, Signature>> m_sigList;
    /// set of the transaction hash
    h256Hash m_transactionHashSet;
    /// m_transactions converted bytes, when m_transactions changed,
    /// should refresh this catch when encode
    bytes m_txsCache;
    /// means whether m_transactions has been changed
    bool m_transaction_changed = false;
};
}  // namespace eth
}  // namespace dev
