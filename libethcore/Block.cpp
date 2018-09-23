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
 * @file Block.cpp
 * @author: yujiechem
 * @date 2018-09-20
 */
#include "Block.h"
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcore/easylog.h>
namespace dev
{
namespace eth
{
Block::Block(bytesConstRef _data)
{
    decode(_data);
}

Block::Block(bytes const& _data)
{
    decode(ref(_data));
}

Block::Block(Block const& _block)
  : m_blockHeader(_block.blockHeader()),
    m_transactions(_block.transactions()),
    m_headerHash(_block.headerHash()),
    m_sigList(_block.sigList())
{
    noteChange();
}

Block& Block::operator=(Block const& _block)
{
    m_blockHeader = _block.blockHeader();
    m_headerHash = _block.headerHash();
    /// init transaction
    m_transactions = _block.transactions();
    /// init sigList
    m_sigList = _block.sigList();
    noteChange();
    return *this;
}

/**
 * @brief : trans the members of Block into Blocks using RLP encode
 *
 * @param _out : generated block
 * @param sig_list: signature list
 */
void Block::encode(bytes& _out, std::vector<std::pair<u256, Signature>>& sig_list)
{
    /// verify blockheader
    m_blockHeader.verify(CheckEverything);
    /// get bytes of BlockHeader
    bytes header_bytes;
    m_blockHeader.encode(header_bytes);
    encode(_out, ref(header_bytes), m_blockHeader.hash(), sig_list);
}

/**
 * @brief : generate block using specified block header and sig_list
 *
 * @param _out : encoded bytes(use rlp encode)
 * @param _header : specified block header to generate the block
 * @param sig_list : signature list
 */
void Block::encode(
    bytes& _out, bytesConstRef _header, std::vector<std::pair<u256, Signature>>& sig_list)
{
    /// check validition of block header before encode
    /// _header data validition has already been checked in "populate of BlockHeader"
    BlockHeader tmpBlockHeader = BlockHeader(_header, HeaderData);
    tmpBlockHeader.verify(CheckEverything);
    encode(_out, _header, tmpBlockHeader.hash(), sig_list);
}

/**
 * @brief : generate block using specified params
 *
 * @param _out : bytes of generated block
 * @param block_header : bytes of the block_header
 * @param hash : hash of the block hash
 * @param sig_list : signature list
 */
void Block::encode(bytes& _out, bytesConstRef block_header, h256 const& hash,
    std::vector<std::pair<u256, Signature>>& sig_list)
{
    /// refresh transaction list cache
    bytes txsCache = encodeTransactions();
    /// get block RLPStream
    RLPStream block_stream;
    block_stream.appendList(4);
    // append block header
    block_stream.appendRaw(block_header);
    // append transaction list
    block_stream.appendRaw(txsCache);
    // append block hash
    block_stream.append(hash);
    // append sig_list
    block_stream.appendVector(sig_list);
    block_stream.swapOut(_out);
}

/// encode transactions to bytes using rlp-encoding when transaction list has been changed
bytes const& Block::encodeTransactions()
{
    /// RecursiveGuard l(m_txsCacheLock);
    RLPStream txs;
    txs.appendList(m_transactions.size());
    if (m_txsCache == bytes())
    {
        for (size_t i = 0; i < m_transactions.size(); i++)
        {
            bytes trans_data;
            m_transactions[i].encode(trans_data);
            txs.appendRaw(trans_data);
        }
        txs.swapOut(m_txsCache);
    }
    return m_txsCache;
}

/**
 * @brief : decode specified data of block into Block class
 * @param _block : the specified data of block
 */
void Block::decode(bytesConstRef _block_bytes)
{
    /// no try-catch to throw exceptions directly
    /// get RLP of block
    RLP block_rlp = BlockHeader::extractBlock(_block_bytes);
    /// get block header
    m_blockHeader.populate(block_rlp[0]);
    /// get transaction list
    RLP transactions_rlp = block_rlp[1];
    m_transactions.resize(transactions_rlp.itemCount());
    for (size_t i = 0; i < transactions_rlp.itemCount(); i++)
    {
        m_transactions[i].decode(transactions_rlp[i]);
    }
    /// get hash of the block header
    m_headerHash = h256(block_rlp[2].toInt<u256>());
    /// get sig_list
    m_sigList = block_rlp[3].toVector<std::pair<u256, Signature>>();
    noteChange();
}
}  // namespace eth
}  // namespace dev
