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
 * @brief : Downloading transactions queue
 * @author: jimmyshi
 * @date: 2019-02-19
 */

#include "DownloadingTxsQueue.h"

using namespace dev;
using namespace dev::sync;

void DownloadingTxsQueue::push(bytesConstRef _txsBytes, NodeID const& _fromPeer)
{
    WriteGuard l(x_buffer);
    m_buffer->emplace_back(_txsBytes, _fromPeer);
}

void DownloadingTxsQueue::pop2TxPool(
    std::shared_ptr<dev::txpool::TxPoolInterface> _txPool, dev::eth::CheckTransaction _checkSig)
{
    // fetch from buffer
    std::shared_ptr<std::vector<DownloadTxsShard>> localBuffer = m_buffer;
    {
        WriteGuard l(x_buffer);
        m_buffer = std::make_shared<std::vector<DownloadTxsShard>>();
    }

    for (size_t i = 0; i < localBuffer->size(); ++i)
    {
        // decode
        dev::eth::Transactions txs;
        DownloadTxsShard const& txsShard = (*localBuffer)[i];
        NodeID fromPeer = txsShard.fromPeer;
        RLP const& txsBytesRLP = RLP(ref(txsShard.txsBytes))[0];
        // std::cout << "decode sync txs " << toHex(txsShard.txsBytes) << std::endl;
        dev::eth::TxsParallelParser::decode(txs, txsBytesRLP.toBytesConstRef(), _checkSig, true);

// parallel verify transaction before import
#pragma omp parallel for
        for (size_t j = 0; j < txs.size(); ++j)
        {
            if (!_txPool->txExists(txs[j].sha3()))
                txs[j].sender();
        }

        // import into tx pool
        size_t successCnt = 0;
        std::vector<dev::h256> knownTxHash;
        auto startTime = utcTime();
        for (auto tx : txs)
        {
            try
            {
                auto importResult = _txPool->import(tx);
                if (dev::eth::ImportResult::Success == importResult)
                    successCnt++;
                else if (dev::eth::ImportResult::AlreadyKnown == importResult)
                {
                    SYNC_LOG(TRACE)
                        << LOG_BADGE("Tx")
                        << LOG_DESC("Import peer transaction into txPool DUPLICATED from peer")
                        << LOG_KV("reason", int(importResult))
                        << LOG_KV("txHash", fromPeer.abridged())
                        << LOG_KV("peer", std::move(tx.sha3().abridged()));
                }
                else
                {
                    SYNC_LOG(TRACE)
                        << LOG_BADGE("Tx")
                        << LOG_DESC("Import peer transaction into txPool FAILED from peer")
                        << LOG_KV("reason", int(importResult))
                        << LOG_KV("txHash", fromPeer.abridged())
                        << LOG_KV("peer", move(tx.sha3().abridged()));
                }
                knownTxHash.push_back(tx.sha3());
            }
            catch (std::exception& e)
            {
                SYNC_LOG(WARNING) << LOG_BADGE("Tx") << LOG_DESC("Invalid transaction RLP recieved")
                                  << LOG_KV("reason", e.what()) << LOG_KV("rlp", toHex(tx.rlp()));
                continue;
            }
            if (knownTxHash.size() > 0)
            {
                _txPool->setTransactionsAreKnownBy(knownTxHash, fromPeer);
            }
        }
        auto pengdingSize = _txPool->pendingSize();
        SYNC_LOG(DEBUG) << LOG_BADGE("Tx") << LOG_DESC("Import peer transactions")
                        << LOG_KV("import", successCnt) << LOG_KV("rcv", txs.size())
                        << LOG_KV("txPool", pengdingSize) << LOG_KV("peer", fromPeer.abridged())
                        << LOG_KV("timeCost", utcTime() - startTime);
    }
}
