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
 * @brief : sync master test
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#pragma once
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/Address.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/Block.h>
#include <libp2p/P2PInterface.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>
#include <libtxpool/TxPoolInterface.h>


namespace dev
{
namespace eth
{
class FakeBlockChain : public dev::blockchain::BlockChainInterface
{
public:
    FakeBlockChain() { m_fakeBlock = std::make_shared<Block>(); }
    virtual int64_t number() const override { return m_number; }
    virtual dev::h256 numberHash(int64_t _i) const override
    {
        return m_fakeBlock->blockHeaderHash();
    }
    virtual std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override
    {
        return m_fakeBlock;
    }
    virtual std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override
    {
        return m_fakeBlock;
    }
    virtual void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override
    {
        (void)block;
        m_number++;
    }

private:
    int64_t m_number = 0;
    std::shared_ptr<Block> m_fakeBlock;
};

class FakeTxPool : public dev::txpool::TxPoolInterface
{
public:
    FakeTxPool(int16_t _protocolId) : m_protocolId(_protocolId) {}

    virtual bool drop(h256 const& _txHash) override { return false; }
    virtual dev::eth::Transactions topTransactions(
        uint64_t const& _limit, h256Hash& _avoid, bool updateAvoid = false) override
    {
        return dev::eth::Transactions();
    }

    virtual dev::eth::Transactions topTransactions(uint64_t const& _limit) override
    {
        return dev::eth::Transactions();
    }
    virtual dev::eth::Transactions pendingList() const override { return dev::eth::Transactions(); }
    virtual size_t pendingSize() override { return 0; }

    virtual std::pair<h256, Address> submit(dev::eth::Transaction& _tx) override
    {
        return std::pair<h256, Address>();
    }

    virtual dev::eth::ImportResult import(
        dev::eth::Transaction& _tx, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) override
    {
        return dev::eth::ImportResult::Success;
    }
    virtual dev::eth::ImportResult import(
        bytesConstRef _txBytes, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) override
    {
        return dev::eth::ImportResult::Success;
    }
    virtual dev::txpool::TxPoolStatus status() const override
    {
        return dev::txpool::TxPoolStatus{0, 0};
    }

    virtual int16_t const& getProtocolId() const override { return m_protocolId; }

private:
    int16_t m_protocolId;
};

}  // namespace eth
}  // namespace dev