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
 * @brief: fake block manager
 * @file: FakeBlockManager.h
 * @author: yujiechen
 * @date: 2018-09-25
 */
#pragma once
#include <libblockchain/BlockChainInterface.h>
#include <libtxpool/TxPool.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::txpool;
using namespace dev::blockchain;

namespace dev
{
namespace test
{
class FakeTxPool : public TxPool
{
public:
    FakeTxPool(std::shared_ptr<dev::p2p::Service> _p2pService,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        uint64_t const& _limit = 102400, int32_t const& _protocolId = dev::eth::ProtocolID::TxPool)
      : TxPool(_p2pService, _blockChain, _protocolId, _limit)
    {}
    ImportResult import(bytesConstRef _txBytes, IfDropped _ik = IfDropped::Ignore)
    {
        return TxPool::import(_txBytes, _ik);
    }
};

class FakeBlockChain : public BlockChainInterface
{
public:
    FakeBlockChain(uint64_t _blockNum, size_t const& transSize = 5) : m_blockNumber(_blockNum)
    {
        FakeTheBlockChain(_blockNum, transSize);
    }

    ~FakeBlockChain() {}
    /// fake the block chain
    void FakeTheBlockChain(uint64_t _blockNum, size_t const& trans_size)
    {
        m_blockChain.clear();
        m_blockHash.clear();
        for (uint64_t blockHeight = 0; blockHeight < _blockNum; blockHeight++)
        {
            FakeBlock fake_block(trans_size);
            if (blockHeight > 0)
            {
                fake_block.m_blockHeader.setParentHash(m_blockChain[blockHeight - 1]->headerHash());
                fake_block.reEncodeDecode();
            }
            m_blockHash[fake_block.m_blockHeader.hash()] = blockHeight;
            m_blockChain.push_back(std::make_shared<Block>(fake_block.m_block));
        }
    }

    int64_t number() const { return m_blockNumber - 1; }

    dev::h256 numberHash(int64_t _i) const { return m_blockChain[_i]->headerHash(); }

    std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
    }

    virtual std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i)
    {
        return getBlockByHash(numberHash(_i));
    }

    virtual void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>)
    {}
    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block> > m_blockChain;
    uint64_t m_blockNumber;
};

}  // namespace test
}  // namespace dev
