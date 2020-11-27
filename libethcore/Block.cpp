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
#include "libdevcrypto/CryptoInterface.h"
#include <libutilities/RLP.h>
#include <tbb/parallel_for.h>


#define BLOCK_LOG(LEVEL)    \
    LOG(LEVEL) << "[Block]" \
               << "[line:" << __LINE__ << "]"

namespace bcos
{
namespace eth
{
Block::Block(
    bytesConstRef _data, CheckTransaction const _option, bool _withReceipt, bool _withTxHash)
  : m_transactions(std::make_shared<Transactions>()),
    m_transactionReceipts(std::make_shared<TransactionReceipts>()),
    m_sigList(nullptr)
{
    m_blockSize = _data.size();
    decode(_data, _option, _withReceipt, _withTxHash);
}

Block::Block(
    bytes const& _data, CheckTransaction const _option, bool _withReceipt, bool _withTxHash)
  : m_transactions(std::make_shared<Transactions>()),
    m_transactionReceipts(std::make_shared<TransactionReceipts>()),
    m_sigList(nullptr)
{
    m_blockSize = _data.size();
    decode(ref(_data), _option, _withReceipt, _withTxHash);
}

Block::Block(Block const& _block)
  : m_blockHeader(_block.blockHeader()),
    m_transactions(std::make_shared<Transactions>(*_block.transactions())),
    m_transactionReceipts(std::make_shared<TransactionReceipts>(*_block.transactionReceipts())),
    m_sigList(std::make_shared<SigListType>(*_block.sigList())),
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
    m_sigList = std::make_shared<SigListType>(*_block.sigList());
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
    // append block hash
    block_stream.append(m_blockHeader.hash());
    // append sig_list
    block_stream.appendVector(*m_sigList);
    // append transactionReceipts list
    block_stream.appendRaw(m_tReceiptsCache);
    block_stream.swapOut(_out);
}

void Block::calTransactionRoot(bool update) const
{
    TIME_RECORD(
        "Calc transaction root, count:" + boost::lexical_cast<std::string>(m_transactions->size()));
    WriteGuard l(x_txsCache);
    if (m_txsCache == bytes())
    {
        std::vector<bcos::bytes> transactionList;
        transactionList.resize(m_transactions->size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_transactions->size()),
            [&](const tbb::blocked_range<size_t>& _r) {
                for (uint32_t i = _r.begin(); i < _r.end(); ++i)
                {
                    RLPStream s;
                    s << i;
                    bcos::bytes byteValue = s.out();
                    bcos::h256 hValue = ((*m_transactions)[i])->hash();
                    byteValue.insert(byteValue.end(), hValue.begin(), hValue.end());
                    transactionList[i] = byteValue;
                }
            });
        m_txsCache = TxsParallelParser::encode(m_transactions);
        m_transRootCache = bcos::getMerkleProofRoot(transactionList);
    }
    if (update == true)
    {
        m_blockHeader.setTransactionsRoot(m_transRootCache);
    }
}

std::shared_ptr<std::map<std::string, std::vector<std::string>>> Block::getTransactionProof() const
{
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> merklePath =
        std::make_shared<std::map<std::string, std::vector<std::string>>>();
    std::vector<bcos::bytes> transactionList;
    transactionList.resize(m_transactions->size());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_transactions->size()),
        [&](const tbb::blocked_range<size_t>& _r) {
            for (uint32_t i = _r.begin(); i < _r.end(); ++i)
            {
                RLPStream s;
                s << i;
                bcos::bytes byteValue = s.out();
                bcos::h256 hValue = ((*m_transactions)[i])->hash();
                byteValue.insert(byteValue.end(), hValue.begin(), hValue.end());
                transactionList[i] = byteValue;
            }
        });

    bcos::getMerkleProof(transactionList, merklePath);
    return merklePath;
}

void Block::getReceiptAndHash(RLPStream& txReceipts, std::vector<bcos::bytes>& receiptList) const
{
    txReceipts.appendList(m_transactionReceipts->size());
    receiptList.resize(m_transactionReceipts->size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_transactionReceipts->size()),
        [&](const tbb::blocked_range<size_t>& _r) {
            for (uint32_t i = _r.begin(); i < _r.end(); ++i)
            {
                (*m_transactionReceipts)[i]->receipt();
                bcos::bytes receiptHash = (*m_transactionReceipts)[i]->hash();
                RLPStream s;
                s << i;
                bcos::bytes receiptValue = s.out();
                receiptValue.insert(receiptValue.end(), receiptHash.begin(), receiptHash.end());
                receiptList[i] = receiptValue;
            }
        });
    for (size_t i = 0; i < m_transactionReceipts->size(); i++)
    {
        txReceipts.appendRaw((*m_transactionReceipts)[i]->receipt());
    }
}

void Block::calReceiptRoot(bool update) const
{
    TIME_RECORD("Calc receipt root, count:" +
                boost::lexical_cast<std::string>(m_transactionReceipts->size()));

    WriteGuard l(x_txReceiptsCache);
    if (m_tReceiptsCache == bytes())
    {
        RLPStream txReceipts;
        std::vector<bcos::bytes> receiptList;
        getReceiptAndHash(txReceipts, receiptList);
        txReceipts.swapOut(m_tReceiptsCache);
        m_receiptRootCache = bcos::getMerkleProofRoot(receiptList);
    }
    if (update == true)
    {
        m_blockHeader.setReceiptsRoot(m_receiptRootCache);
    }
}


std::shared_ptr<std::map<std::string, std::vector<std::string>>> Block::getReceiptProof() const
{
    RLPStream txReceipts;
    std::vector<bcos::bytes> receiptList;
    getReceiptAndHash(txReceipts, receiptList);
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> merklePath =
        std::make_shared<std::map<std::string, std::vector<std::string>>>();
    bcos::getMerkleProof(receiptList, merklePath);
    return merklePath;
}

/**
 * @brief : decode specified data of block into Block class
 * @param _block : the specified data of block
 */
void Block::decode(
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
    m_sigList = std::make_shared<SigListType>(
        block_rlp[3].toVector<std::pair<u256, std::vector<unsigned char>>>());

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

void Block::encodeProposal(std::shared_ptr<bytes> _out, bool const&)
{
    encode(*_out);
}

void Block::decodeProposal(bytesConstRef _block, bool const&)
{
    decode(_block);
}

}  // namespace eth
}  // namespace bcos
