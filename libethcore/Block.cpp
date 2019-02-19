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
 * @author: yujiechem, jimmyshi
 * @date 2018-09-20
 */
#include "Block.h"
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcore/easylog.h>
#include <omp.h>
namespace dev
{
namespace eth
{
class BlockTxsParallelParser
{
    /*
        To support decode transaction parallel in a block
        Encode/Decode Protocol:
        [txNum] [[0], [offset1], [offset2] ...] [[txByte0], [txByte1], [txByte2] ...]
                            |----------------------------------^
    */
    using Offset_t = uint32_t;

public:
    static bytes encode(Transactions& _txs);
    static void decode(Transactions& _txs, bytesConstRef _bytes,
        CheckTransaction _checkSig = CheckTransaction::Everything);

private:
    static inline bytes toBytes(Offset_t _num)
    {
        bytes ret(sizeof(Offset_t), 0);
        *(Offset_t*)(ret.data()) = _num;
        return ret;
    }

    static inline Offset_t fromBytes(bytesConstRef _bs) { return Offset_t(*(Offset_t*)_bs.data()); }
};

Block::Block(bytesConstRef _data, CheckTransaction const option)
{
    decode(_data, option);
}

Block::Block(bytes const& _data, CheckTransaction const option)
{
    decode(ref(_data), option);
}

Block::Block(Block const& _block)
  : m_blockHeader(_block.blockHeader()),
    m_transactions(_block.transactions()),
    m_transactionReceipts(_block.transactionReceipts()),
    m_sigList(_block.sigList()),
    m_txsCache(_block.m_txsCache),
    m_tReceiptsCache(_block.m_tReceiptsCache),
    m_transRootCache(_block.m_transRootCache),
    m_receiptRootCache(_block.m_receiptRootCache)
{}

Block& Block::operator=(Block const& _block)
{
    m_blockHeader = _block.blockHeader();
    /// init transactions
    m_transactions = _block.transactions();
    /// init transactionReceipts
    m_transactionReceipts = _block.transactionReceipts();
    /// init sigList
    m_sigList = _block.sigList();
    m_txsCache = _block.m_txsCache;
    m_tReceiptsCache = _block.m_tReceiptsCache;
    m_transRootCache = _block.m_transRootCache;
    m_receiptRootCache = _block.m_receiptRootCache;
    return *this;
}

/**
 * @brief : generate block using specified params
 *
 * @param _out : bytes of generated block
 * @param block_header : bytes of the block_header
 * @param hash : hash of the block hash
 * @param sig_list : signature list
 */
void Block::encode(bytes& _out) const
{
    m_blockHeader.verify();
    calTransactionRoot(false);
    calReceiptRoot(false);
    bytes headerData;
    m_blockHeader.encode(headerData);
    /// get block RLPStream
    RLPStream block_stream;
    block_stream.appendList(5);
    // append block header
    block_stream.appendRaw(headerData);
    // append transaction list
    block_stream.append(ref(m_txsCache));
    // append transactionReceipts list
    block_stream.appendRaw(m_tReceiptsCache);
    // append block hash
    block_stream.append(m_blockHeader.hash());
    // append sig_list
    block_stream.appendVector(m_sigList);
    block_stream.swapOut(_out);
}


/// encode transactions to bytes using rlp-encoding when transaction list has been changed
void Block::calTransactionRoot(bool update) const
{
    WriteGuard l(x_txsCache);
    RLPStream txs;
    txs.appendList(m_transactions.size());
    if (m_txsCache == bytes())
    {
        m_txsCache = BlockTxsParallelParser::encode(m_transactions);
        m_transRootCache = sha3(m_txsCache);
    }
    if (update == true)
    {
        m_blockHeader.setTransactionsRoot(m_transRootCache);
    }
}

/// encode transactionReceipts to bytes using rlp-encoding when transaction list has been changed
void Block::calReceiptRoot(bool update) const
{
    WriteGuard l(x_txReceiptsCache);
    if (m_tReceiptsCache == bytes())
    {
        RLPStream txReceipts;
        txReceipts.appendList(m_transactionReceipts.size());
        BytesMap mapCache;
        for (size_t i = 0; i < m_transactionReceipts.size(); i++)
        {
            RLPStream s;
            s << i;
            bytes tranReceipts_data;
            m_transactionReceipts[i].encode(tranReceipts_data);
            txReceipts.appendRaw(tranReceipts_data);
            mapCache.insert(std::make_pair(s.out(), tranReceipts_data));
        }
        txReceipts.swapOut(m_tReceiptsCache);
        m_receiptRootCache = hash256(mapCache);
    }
    if (update == true)
    {
        m_blockHeader.setReceiptsRoot(m_receiptRootCache);
    }
}

/**
 * @brief : decode specified data of block into Block class
 * @param _block : the specified data of block
 */
void Block::decode(bytesConstRef _block_bytes, CheckTransaction const option)
{
    /// no try-catch to throw exceptions directly
    /// get RLP of block
    RLP block_rlp = BlockHeader::extractBlock(_block_bytes);
    /// get block header
    m_blockHeader.populate(block_rlp[0]);
    /// get transaction list
    RLP transactions_rlp = block_rlp[1];

    /// get txsCache
    m_txsCache = transactions_rlp.toBytes();

    /// decode transaction
    BlockTxsParallelParser::decode(m_transactions, ref(m_txsCache), option);

    /// get transactionReceipt list
    RLP transactionReceipts_rlp = block_rlp[2];
    m_transactionReceipts.resize(transactionReceipts_rlp.itemCount());
    for (size_t i = 0; i < transactionReceipts_rlp.itemCount(); i++)
    {
        m_transactionReceipts[i].decode(transactionReceipts_rlp[i]);
    }
    /// get hash
    h256 hash = block_rlp[3].toHash<h256>();
    if (hash != m_blockHeader.hash())
    {
        BOOST_THROW_EXCEPTION(ErrorBlockHash() << errinfo_comment("BlockHeader hash error"));
    }
    /// get sig_list
    m_sigList = block_rlp[4].toVector<std::pair<u256, Signature>>();
}

bytes BlockTxsParallelParser::encode(Transactions& _txs)
{
    Offset_t txNum = _txs.size();
    if (txNum == 0)
        return bytes();

    bytes txBytes;
    std::vector<Offset_t> offsets(txNum + 1, 0);
    Offset_t offset = 0;

    // encode tx and caculate offset
    for (Offset_t i = 0; i < txNum; ++i)
    {
        bytes txByte;
        _txs[i].encode(txByte);
        offsets[i] = offset;
        offset += txByte.size();
        txBytes += txByte;
    }
    offsets[txNum] = offset;  // write the end

    // encode according with this protocol
    bytes ret;
    ret += toBytes(Offset_t(txNum));
    for (size_t i = 0; i < offsets.size(); ++i)
        ret += toBytes(offsets[i]);

    ret += txBytes;

    // std::cout << "tx encode:" << toHex(ret) << std::endl;
    return ret;
}

// parallel decode transactions
void BlockTxsParallelParser::decode(
    Transactions& _txs, bytesConstRef _bytes, CheckTransaction _checkSig)
{
    // std::cout << "tx decode:" << toHex(_bytes) << std::endl;
    if (_bytes.size() == 0)
        return;

    Offset_t txNum = fromBytes(_bytes.cropped(0));
    vector_ref<Offset_t> offsets((Offset_t*)_bytes.cropped(sizeof(Offset_t)).data(), txNum + 1);
    _txs.resize(txNum);

    bytesConstRef txBytes = _bytes.cropped(sizeof(Offset_t) * (txNum + 2));

#pragma omp parallel for schedule(dynamic, 1)
    for (Offset_t i = 0; i < txNum; i++)
    {
        Offset_t offset = offsets[i];
        Offset_t size = offsets[i + 1] - offsets[i];
        _txs[i].decode(txBytes.cropped(offset, size), _checkSig);
    }
}


}  // namespace eth
}  // namespace dev
