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
 * @brief : external interface of blockchain
 * @author: mingzhenliu
 * @date: 2018-09-21
 */
#pragma once

#include <libethcore/Block.h>
#include <libethcore/Common.h>
#include <libethcore/Transaction.h>
namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}
namespace blockchain
{
class BlockChainInterface
{
public:
    BlockChainInterface() = default;
    virtual ~BlockChainInterface(){};
    virtual int64_t number() const = 0;
    virtual dev::h256 numberHash(int64_t _i) const = 0;
    virtual dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) = 0;
    virtual dev::eth::TransactionReceipt getTransactionReceiptByHash(dev::h256 const& _txHash) = 0;
    virtual std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) = 0;
    virtual std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) = 0;
    virtual void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) = 0;

    /// Register a handler that will be called once there is a new transaction imported
    template <class T>
    dev::eth::Handler<> onReady(T const& _t)
    {
        return m_onReady.add(_t);
    }

protected:
    ///< Called when a subsequent call to import transactions will return a non-empty container. Be
    ///< nice and exit fast.
    dev::eth::Signal<> m_onReady;
};
}  // namespace blockchain
}  // namespace dev
