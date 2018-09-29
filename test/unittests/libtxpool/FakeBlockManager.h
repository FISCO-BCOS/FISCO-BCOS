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
#include <libblockmanager/BlockManagerInterface.h>
#include <libblockmanager/NonceCheck.h>
#include <libtxpool/TxPool.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::txpool;
using namespace dev::blockmanager;

namespace dev
{
namespace test
{
class FakeTxPool : public TxPool
{
public:
    FakeTxPool(std::shared_ptr<dev::p2p::Service> _p2pService,
        std::shared_ptr<dev::blockmanager::BlockManagerInterface> _blockManager,
        uint64_t const& _limit = 102400, int32_t const& _protocolId = dev::eth::ProtocolID::TxPool)
      : TxPool(_p2pService, _blockManager, _protocolId, _limit)
    {}
    ImportResult import(bytesConstRef _txBytes, IfDropped _ik = IfDropped::Ignore)
    {
        return TxPool::import(_txBytes, _ik);
    }
};

class FakeBlockManager : public BlockManagerInterface
{
public:
    FakeBlockManager(uint64_t _blockNum, size_t const& transSize = 5) : m_blockNumber(_blockNum)
    {
        FakeBlockChain(_blockNum, transSize);
    }

    ~FakeBlockManager() {}
    void setNonceCheck(std::shared_ptr<NonceCheck> const& _nonceCheck)
    {
        m_nonceCheck = _nonceCheck;
    }

    /// fake block chain
    void FakeBlockChain(uint64_t _blockNum, size_t const& trans_size)
    {
        m_blockChain.clear();
        m_blockHash.clear();
        for (uint64_t blockHeight = 0; blockHeight <= _blockNum; blockHeight++)
        {
            FakeBlock fake_block(trans_size);
            if (blockHeight > 0)
            {
                fake_block.m_blockHeader.setParentHash(m_blockChain[blockHeight - 1].headerHash());
                fake_block.reEncodeDecode();
            }
            m_blockChain.push_back(fake_block.m_block);
            m_blockHash[fake_block.m_blockHeader.hash()] = blockHeight;
        }
    }

    uint64_t number() const { return m_blockNumber; }

    h256 numberHash(unsigned _i) const { return m_blockChain[_i].headerHash(); }

    std::vector<bytes> transactions(h256 const& _blockHash)
    {
        std::vector<bytes> ret;
        if (m_blockHash.count(_blockHash))
        {
            bytes block_data;
            m_blockChain[m_blockHash[_blockHash]].encode(block_data);
            for (auto const& i : RLP(block_data)[1])
                ret.push_back(i.data().toBytes());
        }
        return ret;
    }

    bool isNonceOk(dev::eth::Transaction const& _transaction, bool _needinsert = false)
    {
        if (auto nonce_check = m_nonceCheck.lock())
            return nonce_check->ok(_transaction, _needinsert);
        return false;
    }
    std::weak_ptr<NonceCheck> m_nonceCheck;
    std::map<h256, uint64_t> m_blockHash;
    std::vector<Block> m_blockChain;
    uint64_t m_blockNumber;
};

}  // namespace test
}  // namespace dev
