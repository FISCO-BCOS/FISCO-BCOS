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
 * @file: TransactionNonceCheck.h
 * @author: toxotguo
 *
 * @date: 2017
 */

#pragma once
#include "CommonTransactionNonceCheck.h"
#include <libblockchain/BlockChainInterface.h>
#include <boost/timer.hpp>
#include <thread>

using namespace dev::eth;
using namespace dev::blockchain;

#define NONCECHECKER_LOG(LEVEL) \
    LOG(LEVEL) << LOG_BADGE("TXPOOL") << LOG_BADGE("TransactionNonceChecker")

namespace dev
{
namespace txpool
{
using NonceVec = std::vector<dev::eth::NonceKeyType>;
class TransactionNonceCheck : public CommonTransactionNonceCheck
{
public:
    TransactionNonceCheck(std::shared_ptr<dev::blockchain::BlockChainInterface> const& _blockChain)
      : CommonTransactionNonceCheck(), m_blockChain(_blockChain)
    {
        init();
    }
    ~TransactionNonceCheck() {}
    void init();
    bool ok(dev::eth::Transaction const& _transaction);
    void updateCache(bool _rebuild = false);
    unsigned const& maxBlockLimit() const { return m_maxBlockLimit; }
    void setBlockLimit(unsigned const& limit) { m_maxBlockLimit = limit; }

    bool isBlockLimitOk(dev::eth::Transaction const& _trans);

    void getNonceAndUpdateCache(
        NonceVec& nonceVec, int64_t const& blockNumber, bool const& update = true);

private:
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    std::vector<NonceVec> nonce_vec;
    /// cache the block nonce to in case of accessing the DB to get nonces of given block frequently
    /// key: block number
    /// value: all the nonces of a given block
    /// we cache at most m_maxBlockLimit entries(occuppy about 32KB)
    std::map<int64_t, NonceVec> m_blockNonceCache;

    int64_t m_startblk;
    int64_t m_endblk;
    unsigned m_maxBlockLimit = 1000;
    int64_t m_blockNumber;
};
}  // namespace txpool
}  // namespace dev
