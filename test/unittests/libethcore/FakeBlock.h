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
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace test
{
class FakeBlock
{
public:
    /// for normal case test
    FakeBlock(size_t size)
    {
        FakeBlockHeader();
        FakeSigList(size);
        FakeTransaction(size);
        m_block.setSigList(m_sigList);
        m_block.setTransactions(m_transaction);
        BOOST_CHECK(m_transaction == m_block.transactions());
        BOOST_CHECK(m_sigList == m_block.sigList());
        m_blockHeader.encode(m_blockHeaderData);
        m_block.encode(m_blockData, ref(m_blockHeaderData));
        m_block.decode(ref(m_blockData));
    }
    /// for empty case test
    FakeBlock()
    {
        m_blockHeader.encode(m_blockHeaderData);
        m_block.encode(m_blockData, ref(m_blockHeaderData));
        m_block.decode(ref(m_blockData));
    }

    /// fake invalid block data
    bytes FakeInvalidBlockData(size_t index, size_t size)
    {
        bytes result;
        RLPStream s;
        s.appendList(4);
        FakeBlockHeader();
        FakeSigList(size);
        FakeTransaction(size);
        unsigned fake_value = 10;
        // s << fake_value << fake_value << fake_value << fake_value;
        if (index == 1)
            s.append(fake_value);
        else
            s.appendRaw(m_blockHeaderData);
        if (index == 2)
            s.append(fake_value);
        else
            s.appendRaw(m_transactionData);
        s.append(m_blockHeader.hash());
        s.appendVector(m_sigList);
        s.swapOut(result);
        return result;
    }

    /// check exception conditions related about decode and encode
    void CheckInvalidBlockData(size_t size)
    {
        FakeBlockHeader();
        BOOST_CHECK_THROW(m_block.decode(ref(m_blockHeaderData)), InvalidBlockFormat);
        BOOST_REQUIRE_NO_THROW(m_block.encode(m_blockData, ref(m_blockHeaderData)));
        m_blockHeader.setGasUsed(u256(3000000000));
        m_blockHeader.encode(m_blockHeaderData);
        BOOST_CHECK_THROW(m_block.encode(m_blockData, ref(m_blockHeaderData)), TooMuchGasUsed);
        m_blockHeaderData[0] += 1;
        /// construct block header failed, invalid rlp
        BOOST_CHECK_THROW(m_block.encode(m_blockData, ref(m_blockHeaderData)), std::exception);
        m_blockHeaderData[0] -= 1;
        /// construct invalid block format
        for (size_t i = 1; i < 3; i++)
        {
            bytes result_bytes = FakeInvalidBlockData(i, size);
            BOOST_CHECK_THROW(m_block.decode(ref(result_bytes)), InvalidBlockFormat);
        }
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

    /// fake sig list
    void FakeSigList(size_t size)
    {
        /// set sig list
        Signature sig;
        h256 block_hash;
        Secret sec = KeyPair::create().secret();
        for (size_t i = 0; i < size; i++)
        {
            block_hash = sha3(toString("block " + i));
            sig = sign(sec, block_hash);
            m_sigList.push_back(std::make_pair(u256(block_hash), sig));
        }
    }

    /// fake transactions
    void FakeTransaction(size_t size)
    {
        fakeSingleTransaction();
        RLPStream txs;
        txs.appendList(size);
        m_transaction.resize(size);
        for (size_t i = 0; i < size; i++)
        {
            m_transaction[i] = m_singleTransaction;
            bytes trans_data;
            m_transaction[i].encode(trans_data);
            txs.appendRaw(trans_data);
        }
        txs.swapOut(m_transactionData);
    }

    /// fake single transaction
    void fakeSingleTransaction()
    {
        bytes rlpBytes = fromHex(
            "f8ac8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a10000000"
            "0000"
            "000000000000003ca576d469d7aa0244071d27eb33c5629753593e00000000000000000000000000000000"
            "0000"
            "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62"
            "657c"
            "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df050948203ea");

        RLP rlpObj(rlpBytes);
        bytesConstRef d = rlpObj.data();
        m_singleTransaction = Transaction(d, eth::CheckTransaction::Everything);
    }

    Block& getBlock() { return m_block; }
    BlockHeader& getBlockHeader() { return m_blockHeader; }
    bytes& getBlockHeaderData() { return m_blockHeaderData; }
    bytes& getBlockData() { return m_blockData; }

public:
    Block m_block;
    BlockHeader m_blockHeader;
    Transactions m_transaction;
    Transaction m_singleTransaction;
    std::vector<std::pair<u256, Signature>> m_sigList;
    bytes m_blockHeaderData;
    bytes m_blockData;
    bytes m_transactionData;
};

}  // namespace test
}  // namespace dev
