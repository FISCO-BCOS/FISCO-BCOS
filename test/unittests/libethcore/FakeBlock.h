/**
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
 *
 * @brief
 *
 * @file FakeBlock.h
 * @author: yujiechen
 * @date 2018-09-21
 */
#pragma once
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>

using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace test
{
class FakeBlock
{
public:
    FakeBlock()
    {
        FakeBlockHeader();
        m_blockHeader.encode(m_blockHeaderData);
        m_block.encode(m_blockData, ref(m_blockHeaderData));
        m_block.decode(ref(m_blockData));
    }

    /// fake block header
    void FakeBlockHeader()
    {
        m_blockHeader.setParentHash(sha3("parent"));
        m_blockHeader.setRoots(sha3("transactionRoot"), sha3("receiptRoot"), sha3("stateRoot"));
        m_blockHeader.setLogBloom(LogBloom(0));
        m_blockHeader.setNumber(int64_t(0));
        m_blockHeader.setGasLimit(u256(3000000));
        m_blockHeader.setGasUsed(u256(100000));
        uint64_t current_time = utcTime();
        m_blockHeader.setTimestamp(current_time);
        m_blockHeader.appendExtraDataArray(jsToBytes("0x1020"));
        m_blockHeader.setSealer(u256("0x00"));
        std::vector<h512> sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(toPublic(Secret::random()));
        }
        m_blockHeader.setSealerList(sealer_list);
    }
    Block& getBlock() { return m_block; }
    BlockHeader& getBlockHeader() { return m_blockHeader; }
    bytes& getBlockHeaderData() { return m_blockHeaderData; }
    bytes& getBlockData() { return m_blockData; }

private:
    Block m_block;
    BlockHeader m_blockHeader;
    Transactions m_transaction;
    std::vector<std::pair<u256, Signature>> m_sigList;
    bytes m_blockHeaderData;
    bytes m_blockData;
};
}  // namespace test
}  // namespace dev
