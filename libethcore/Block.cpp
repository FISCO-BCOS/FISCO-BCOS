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
#include "TxsParallelParser.h"
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <tbb/parallel_for.h>


#define BLOCK_LOG(LEVEL)    \
    LOG(LEVEL) << "[Block]" \
               << "[line:" << __LINE__ << "]"

namespace dev
{
namespace eth
{
Block::Block(
    bytesConstRef _data, CheckTransaction const _option, bool _withReceipt, bool _withTxHash)
{
    m_transactions = std::make_shared<Transactions>();
    m_transactionReceipts = std::make_shared<TransactionReceipts>();
    m_sigList = std::make_shared<std::vector<std::pair<u256, Signature>>>();

    decode(_data, _option, _withReceipt, _withTxHash);
}

Block::Block(
    bytes const& _data, CheckTransaction const _option, bool _withReceipt, bool _withTxHash)
{
    m_transactions = std::make_shared<Transactions>();
    m_transactionReceipts = std::make_shared<TransactionReceipts>();
    m_sigList = std::make_shared<std::vector<std::pair<u256, Signature>>>();

    decode(ref(_data), _option, _withReceipt, _withTxHash);
}

Block::Block(Block const& _block)
  : m_blockHeader(_block.blockHeader()),
    m_transactions(std::make_shared<Transactions>(*_block.transactions())),
    m_transactionReceipts(std::make_shared<TransactionReceipts>(*_block.transactionReceipts())),
    m_sigList(std::make_shared<std::vector<std::pair<u256, Signature>>>(*_block.sigList())),
    m_txsCache(_block.m_txsCache),
    m_tReceiptsCache(_block.m_tReceiptsCache),
    m_transRootCache(_block.m_transRootCache),
    m_receiptRootCache(_block.m_receiptRootCache)
{}

Block& Block::operator=(Block const& _block)
{
    m_blockHeader = _block.blockHeader();
    /// init transactions
    m_transactions = std::make_shared<Transactions>(*_block.transactions());
    /// init transactionReceipts
    m_transactionReceipts = std::make_shared<TransactionReceipts>(*_block.transactionReceipts());
    /// init sigList
    m_sigList = std::make_shared<std::vector<std::pair<u256, Signature>>>(*_block.sigList());
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
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        encodeRC2(_out);
        return;
    }

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
    block_stream.appendRaw(m_txsCache);
    // append transactionReceipts list
    block_stream.appendRaw(m_tReceiptsCache);
    // append block hash
    block_stream.append(m_blockHeader.hash());
    // append sig_list
    block_stream.appendVector(*m_sigList);
    block_stream.swapOut(_out);
}

void Block::encodeRC2(bytes& _out) const
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
    // append block hash
    block_stream.append(m_blockHeader.hash());
    // append sig_list
    block_stream.appendVector(*m_sigList);
    // append transactionReceipts list
    block_stream.appendRaw(m_tReceiptsCache);
    block_stream.swapOut(_out);
}


/// encode transactions to bytes using rlp-encoding when transaction list has been changed
void Block::calTransactionRoot(bool update) const
{
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        calTransactionRootV2_2_0(update);
        return;
    }
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        calTransactionRootRC2(update);
        return;
    }

    WriteGuard l(x_txsCache);
    RLPStream txs;
    txs.appendList(m_transactions->size());
    if (m_txsCache == bytes())
    {
        BytesMap txsMapCache;
        for (size_t i = 0; i < m_transactions->size(); i++)
        {
            RLPStream s;
            s << i;
            bytes trans_data;
            (*m_transactions)[i]->encode(trans_data);
            txs.appendRaw(trans_data);
            txsMapCache.insert(std::make_pair(s.out(), trans_data));
        }
        txs.swapOut(m_txsCache);
        m_transRootCache = hash256(txsMapCache);
    }
    if (update == true)
    {
        m_blockHeader.setTransactionsRoot(m_transRootCache);
    }
}


std::map<std::string, std::vector<std::string>> Block::calTransactionRootV2_2_0(
    bool update, bool calForce) const
{
    std::map<std::string, std::vector<std::string>> merklePath;
    WriteGuard l(x_txsCache);
    if (m_txsCache == bytes() || calForce)
    {
        BytesMap txsMapCache;
        for (size_t i = 0; i < m_transactions->size(); i++)
        {
            RLPStream s;
            s << i;
            bytes trans_data;
            (*m_transactions)[i]->encode(trans_data);
            txsMapCache.insert(std::make_pair(s.out(), trans_data));
        }
        m_txsCache = TxsParallelParser::encode(m_transactions);
        m_transRootCache = dev::getHash256(txsMapCache, merklePath);
    }
    if (update == true)
    {
        m_blockHeader.setTransactionsRoot(m_transRootCache);
    }

    return merklePath;
}

std::map<std::string, std::vector<std::string>> Block::calReceiptRootV2_2_0(
    bool update, bool calForce) const
{
    std::map<std::string, std::vector<std::string>> merklePath;
    WriteGuard l(x_txReceiptsCache);
    if (m_tReceiptsCache == bytes() || calForce)
    {
        RLPStream txReceipts;
        txReceipts.appendList(m_transactionReceipts->size());
        BytesMap mapCache;
        for (size_t i = 0; i < m_transactionReceipts->size(); i++)
        {
            bytes tranReceipts_data;
            (*m_transactionReceipts)[i]->encode(tranReceipts_data);
            txReceipts.appendRaw(tranReceipts_data);
            RLPStream s;
            s << i;
            mapCache.insert(std::make_pair(s.out(), tranReceipts_data));
        }
        txReceipts.swapOut(m_tReceiptsCache);
        m_receiptRootCache = dev::getHash256(mapCache, merklePath);
    }
    if (update == true)
    {
        m_blockHeader.setReceiptsRoot(m_receiptRootCache);
    }

    return merklePath;
}


void Block::calTransactionRootRC2(bool update) const
{
    WriteGuard l(x_txsCache);
    if (m_txsCache == bytes())
    {
        m_txsCache = TxsParallelParser::encode(m_transactions);
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
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        calReceiptRootV2_2_0(update);
        return;
    }
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        calReceiptRootRC2(update);
        return;
    }
    WriteGuard l(x_txReceiptsCache);
    if (m_tReceiptsCache == bytes())
    {
        RLPStream txReceipts;
        txReceipts.appendList(m_transactionReceipts->size());
        BytesMap mapCache;
        for (size_t i = 0; i < m_transactionReceipts->size(); i++)
        {
            RLPStream s;
            s << i;
            bytes tranReceipts_data;
            (*m_transactionReceipts)[i]->encode(tranReceipts_data);
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

void Block::calReceiptRootRC2(bool update) const
{
    WriteGuard l(x_txReceiptsCache);
    if (m_tReceiptsCache == bytes())
    {
        size_t receiptsNum = m_transactionReceipts->size();

        std::vector<dev::bytes> receiptsRLPs(receiptsNum, bytes());
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, receiptsNum), [&](const tbb::blocked_range<size_t>& _r) {
                for (size_t i = _r.begin(); i != _r.end(); ++i)
                {
                    RLPStream s;
                    s << i;
                    dev::bytes receiptRLP;
                    (*m_transactionReceipts)[i]->encode(receiptRLP);
                    receiptsRLPs[i] = receiptRLP;
                }
            });

        // auto record_time = utcTime();
        RLPStream txReceipts;
        txReceipts.appendList(receiptsNum);
        for (size_t i = 0; i < receiptsNum; ++i)
        {
            txReceipts.appendRaw(receiptsRLPs[i]);
        }
        txReceipts.swapOut(m_tReceiptsCache);
        // auto appenRLP_time_cost = utcTime() - record_time;
        // record_time = utcTime();

        m_receiptRootCache = dev::sha3(ref(m_tReceiptsCache));
        // auto hashReceipts_time_cost = utcTime() - record_time;
        /*
        LOG(DEBUG) << LOG_BADGE("Receipt") << LOG_DESC("Calculate receipt root cost")
                   << LOG_KV("appenRLPTimeCost", appenRLP_time_cost)
                   << LOG_KV("hashReceiptsTimeCost", hashReceipts_time_cost)
                   << LOG_KV("receipts num", receiptsNum);
                   */
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
void Block::decode(
    bytesConstRef _block_bytes, CheckTransaction const _option, bool _withReceipt, bool _withTxHash)
{
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        decodeRC2(_block_bytes, _option, _withReceipt, _withTxHash);
        return;
    }

    /// no try-catch to throw exceptions directly
    /// get RLP of block
    RLP block_rlp = BlockHeader::extractBlock(_block_bytes);
    /// get block header
    m_blockHeader.populate(block_rlp[0]);
    /// get transaction list
    RLP transactions_rlp = block_rlp[1];

    m_transactions->resize(transactions_rlp.itemCount());
    for (size_t i = 0; i < transactions_rlp.itemCount(); i++)
    {
        (*m_transactions)[i]->decode(transactions_rlp[i], _option);
    }

    /// get txsCache
    m_txsCache = transactions_rlp.data().toBytes();

    /// get transactionReceipt list
    RLP transactionReceipts_rlp = block_rlp[2];
    m_transactionReceipts->resize(transactionReceipts_rlp.itemCount());
    for (size_t i = 0; i < transactionReceipts_rlp.itemCount(); i++)
    {
    	(*m_transactionReceipts)[i] = std::make_shared<dev::eth::TransactionReceipt>();
        (*m_transactionReceipts)[i]->decode(transactionReceipts_rlp[i]);
    }
    /// get hash
    h256 hash = block_rlp[3].toHash<h256>();
    if (hash != m_blockHeader.hash())
    {
        BOOST_THROW_EXCEPTION(ErrorBlockHash() << errinfo_comment("BlockHeader hash error"));
    }
    /// get sig_list
    m_sigList = std::make_shared<std::vector<std::pair<u256, Signature>>>(
        block_rlp[4].toVector<std::pair<u256, Signature>>());
}

void Block::decodeRC2(
    bytesConstRef _block_bytes, CheckTransaction const _option, bool _withReceipt, bool _withTxHash)
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
    TxsParallelParser::decode(m_transactions, ref(m_txsCache), _option, _withTxHash);

    /// get hash
    h256 hash = block_rlp[2].toHash<h256>();
    if (hash != m_blockHeader.hash())
    {
        BOOST_THROW_EXCEPTION(ErrorBlockHash() << errinfo_comment("BlockHeader hash error"));
    }
    /// get sig_list
    m_sigList = std::make_shared<std::vector<std::pair<u256, Signature>>>(
        block_rlp[3].toVector<std::pair<u256, Signature>>());

    /// get transactionReceipt list
    if (_withReceipt)
    {
        RLP transactionReceipts_rlp = block_rlp[4];
        m_transactionReceipts->resize(transactionReceipts_rlp.itemCount());
        for (size_t i = 0; i < transactionReceipts_rlp.itemCount(); i++)
        {
        	(*m_transactionReceipts)[i] = std::make_shared<TransactionReceipt>();
            (*m_transactionReceipts)[i]->decode(transactionReceipts_rlp[i]);
        }
    }
}

}  // namespace eth
}  // namespace dev
