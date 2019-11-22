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
#include "PartiallyBlock.h"
#include "TxsParallelParser.h"
#include <tbb/parallel_for.h>

using namespace dev;
using namespace dev::eth;

void PartiallyBlock::encodeProposal(std::shared_ptr<bytes> _out, bool const& _onlyTxsHash)
{
    if (!_onlyTxsHash)
    {
        return Block::encodeProposal(_out, _onlyTxsHash);
    }
    m_blockHeader.verify();
    calTransactionRoot(false);
    calReceiptRoot(false);
    bytes headerData;
    m_blockHeader.encode(headerData);
    /// get block RLPStream
    RLPStream block_stream;
    block_stream.appendList(3);
    // append block header
    block_stream.appendRaw(headerData);
    calTxsHashBytes();
    // append transaction hash list
    block_stream.appendVector(*m_txsHash);
    // append block hash
    block_stream.append(m_blockHeader.hash());
    block_stream.swapOut(*_out);
}

void PartiallyBlock::calTxsHashBytes()
{
    std::shared_ptr<dev::bytes> txsHashBytes = std::make_shared<dev::bytes>();
    // get transaction hash
    m_txsHash->resize(m_transactions->size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_transactions->size()),
        [&](const tbb::blocked_range<size_t>& _r) {
            for (uint32_t i = _r.begin(); i < _r.end(); ++i)
            {
                (*m_txsHash)[i] = (*m_transactions)[i]->sha3();
            }
        });
}

void PartiallyBlock::decodeProposal(bytesConstRef _blockBytes, bool const& _onlyTxsHash)
{
    if (!_onlyTxsHash)
    {
        return Block::decodeProposal(_blockBytes, _onlyTxsHash);
    }
    // decode blockHeader
    RLP blockRlp = BlockHeader::extractBlock(_blockBytes);
    m_blockHeader.populate(blockRlp[0]);
    // decode transaction hash
    *m_txsHash = blockRlp[1].toVector<dev::h256>();
    // decode and check blockHash
    dev::h256 blockHash = blockRlp[2].toHash<h256>(RLP::VeryStrict);
    if (blockHash != m_blockHeader.hash())
    {
        PartiallyBlock_LOG(ERROR) << LOG_DESC("decodeProposal failed for inconsistent block hash")
                                  << LOG_KV("proposalHash", blockHash.abridged())
                                  << LOG_KV("headerHash", m_blockHeader.hash().abridged());
        BOOST_THROW_EXCEPTION(ErrorBlockHash() << errinfo_comment("BlockHeader hash error"));
    }
    m_transactions->resize(m_txsHash->size());
}

void PartiallyBlock::checkBasic(RLP const& rlp, dev::h256 const& _expectedHash)
{
    // check block number
    int64_t blockNumber = rlp[0].toPositiveInt64();
    if (blockNumber != m_blockHeader.number())
    {
        PartiallyBlock_LOG(ERROR) << LOG_DESC("checkBasic failed for inconsistent block number")
                                  << LOG_KV("proposalNumber", blockNumber)
                                  << LOG_KV("headerNumber", m_blockHeader.number());
        BOOST_THROW_EXCEPTION(
            InvalidNumber() << errinfo_comment("checkBasic: invalid block number"));
    }

    // check block hash
    auto blockHash = rlp[1].toHash<h256>(RLP::VeryStrict);
    if (blockHash != _expectedHash)
    {
        PartiallyBlock_LOG(ERROR) << LOG_DESC("checkBasic failed for inconsistent block hash")
                                  << LOG_KV("expectedHash", _expectedHash.abridged())
                                  << LOG_KV("proposalHash", blockHash.abridged());
        BOOST_THROW_EXCEPTION(
            ErrorBlockHash() << errinfo_comment("checkBasic: inconsistent block hash"));
    }
}

// fill the missed transactions into the block
void PartiallyBlock::fillBlock(bytesConstRef _txsData)
{
    m_missedTransactions->clear();
    RLP blockRlp(_txsData);
    // check block number
    checkBasic(blockRlp, m_blockHeader.hash());

    auto txsBytes = blockRlp[2].toBytesConstRef();
    // decode transactions into m_missedTransactions
    TxsParallelParser::decode(m_missedTransactions, txsBytes, CheckTransaction::Everything, true);

    if (m_missedTransactions->size() != m_missedTxs->size())
    {
        PartiallyBlock_LOG(ERROR) << LOG_DESC("fillBlock failed for inconsistent transaction count")
                                  << LOG_KV("expectedTxsSize", m_missedTxs->size())
                                  << LOG_KV("fetchedTxsSize", m_missedTransactions->size());
        BOOST_THROW_EXCEPTION(InvalidTransaction() << errinfo_comment(
                                  "invalid fetched transactions: inconsistent transaction count"));
    }

    // check and fetch missed transaction into transactions
    int64_t index = 0;
    for (auto tx : *m_missedTransactions)
    {
        if (tx->sha3() != (*m_missedTxs)[index].first)
        {
            PartiallyBlock_LOG(ERROR)
                << LOG_DESC("fillBlock failed for inconsistent transaction hash")
                << LOG_KV("expectedHash", (*m_missedTxs)[index].first.abridged())
                << LOG_KV("fetchedHash", tx->sha3().abridged());
            BOOST_THROW_EXCEPTION(
                InvalidTransaction() << errinfo_comment(
                    "fillBlock: invalid fetched transaction for inconsistent transaction hash"));
        }
        (*m_transactions)[(*m_missedTxs)[index].second] = tx;
        index++;
    }
}

// the missedInfo include:
// 1. blockNumber
// 2. blockHash
// 3. hash of missed transactions
void PartiallyBlock::encodeMissedInfo(std::shared_ptr<bytes> _encodedBytes)
{
    assert(m_missedTxs->size() > 0);
    RLPStream txsStream;
    txsStream.appendList(3);
    txsStream << m_blockHeader.number() << m_blockHeader.hash();
    txsStream.appendVector(*m_missedTxs);
    txsStream.swapOut(*_encodedBytes);
}

// encode the missed transaction when receive fetch-txs request
// the response incude:
// 1. blockNumber
// 2. blockHash
// 3. encodedData of missed transactions
void PartiallyBlock::fetchMissedTxs(
    std::shared_ptr<bytes> _encodedBytes, bytesConstRef _missInfo, dev::h256 const& _expectedHash)
{
    if (m_missedTransactions->size() > 0)
    {
        PartiallyBlock_LOG(ERROR) << LOG_DESC(
            "fetchMissedTxs failed for the block-self does not has complete transactions");
        BOOST_THROW_EXCEPTION(
            NotCompleteBlock() << errinfo_comment(
                "fetchMissedTxs: the block-self does not has complete transactions"));
    }
    // decode _missInfo
    RLP missedInfoRlp(_missInfo);
    // Note: since the blockHash maybe changed after the block executed,
    // so should check the block_hash of rawPrepareCache
    checkBasic(missedInfoRlp, _expectedHash);

    std::shared_ptr<std::vector<std::pair<dev::h256, uint64_t>>> missedTxs =
        std::make_shared<std::vector<std::pair<dev::h256, uint64_t>>>();

    *missedTxs = missedInfoRlp[2].toVector<std::pair<dev::h256, uint64_t>>();

    encodeMissedTxs(_encodedBytes, missedTxs, _expectedHash);
}


void PartiallyBlock::encodeMissedTxs(std::shared_ptr<bytes> _encodedBytes,
    std::shared_ptr<std::vector<std::pair<dev::h256, uint64_t>>> _missedTxs,
    dev::h256 const& _expectedHash)
{
    std::shared_ptr<Transactions> missedTransactions = std::make_shared<Transactions>();

    // obtain missed transaction
    for (auto const& txsInfo : *_missedTxs)
    {
        auto tx = (*m_transactions)[txsInfo.second];
        if (tx->sha3() != txsInfo.first)
        {
            PartiallyBlock_LOG(ERROR)
                << LOG_DESC("fetchMissedTxs failed for inconsistent transaction hash")
                << LOG_KV("requestHash", txsInfo.first.abridged())
                << LOG_KV("txHash", tx->sha3().abridged());
            BOOST_THROW_EXCEPTION(
                InvalidTransaction() << errinfo_comment(
                    "encodeMissedTxs: invalid transaction for inconsistent transaction hash"));
        }
        missedTransactions->emplace_back(tx);
    }
    PartiallyBlock_LOG(DEBUG) << LOG_DESC("fetchMissedTxs")
                              << LOG_KV("hash", _expectedHash.abridged())
                              << LOG_KV("number", m_blockHeader.number())
                              << LOG_KV("txsSize", missedTransactions->size());
    // encode the missedTransactions
    RLPStream txsStream;
    txsStream.appendList(3);
    // Note: since the blockHash maybe changed after the block executed,
    // so should set the block_hash of rawPrepareCache into the hash field
    txsStream << m_blockHeader.number() << _expectedHash;
    auto txsBytes = TxsParallelParser::encode(missedTransactions);

    txsStream.append(ref(txsBytes));
    txsStream.swapOut(*_encodedBytes);
}