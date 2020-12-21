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
#include <libdevcrypto/CryptoInterface.h>
#include <libprotocol/Block.h>
#include <libprotocol/BlockFactory.h>
#include <libprotocol/BlockHeader.h>
#include <libprotocol/Transaction.h>
#include <libprotocol/TxsParallelParser.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
class FakeBlock
{
public:
    /// for normal case test
    FakeBlock(size_t size, KeyPair const& keyPair = KeyPair::create(), uint64_t blockNumber = 0,
        std::shared_ptr<BlockFactory> _blockFactory = nullptr)
    {
        m_sigList = std::make_shared<std::vector<std::pair<u256, std::vector<unsigned char>>>>();
        m_transaction = std::make_shared<Transactions>();
        m_transactionReceipt = std::make_shared<TransactionReceipts>();
        m_keyPair = keyPair;
        FakeBlockHeader(blockNumber);
        FakeSigList(size);
        FakeTransaction(size);
        FakeTransactionReceipt(size);
        if (_blockFactory)
        {
            m_block = _blockFactory->createBlock();
        }
        else
        {
            m_block = std::make_shared<Block>();
        }
        m_block->setSigList(m_sigList);
        m_block->setTransactions(m_transaction);
        m_block->setBlockHeader(m_blockHeader);
        m_block->setTransactionReceipts(m_transactionReceipt);
        BOOST_CHECK(*m_transaction == *m_block->transactions());
        BOOST_CHECK(*m_sigList == *(m_block->sigList()));
        BOOST_CHECK(m_blockHeader = m_block->header());
        m_block->encode(m_blockData);
    }

    /// for empty case test
    FakeBlock(std::shared_ptr<BlockFactory> _blockFactory = nullptr)
    {
        m_sigList = std::make_shared<std::vector<std::pair<u256, std::vector<unsigned char>>>>();
        m_transaction = std::make_shared<Transactions>();
        m_transactionReceipt = std::make_shared<TransactionReceipts>();
        if (_blockFactory)
        {
            m_block = _blockFactory->createBlock();
        }
        else
        {
            m_block = std::make_shared<Block>();
        }
        m_block->setBlockHeader(m_blockHeader);
        m_block->encode(m_blockData);
        m_keyPair = KeyPair::create();
    }

    /// fake invalid block data
    bytes FakeInvalidBlockData(size_t index, size_t size)
    {
        bytes result;
        RLPStream s;
        s.appendList(4);
        FakeBlockHeader(0);
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
            s.append(ref(m_transactionData));
        s.append(m_blockHeader.hash());
        s.appendVector(*m_sigList);
        s.swapOut(result);
        return result;
    }

    /// check exception conditions related about decode and encode
    void CheckInvalidBlockData(size_t size)
    {
        FakeBlockHeader(0);
        m_block->setBlockHeader(m_blockHeader);
        BOOST_CHECK_THROW(m_block->decode(ref(m_blockHeaderData)), InvalidBlockFormat);
        BOOST_REQUIRE_NO_THROW(m_block->encode(m_blockData));
        m_block->header().setGasUsed(u256(3000000000));
        m_blockHeader.encode(m_blockHeaderData);
        BOOST_CHECK_THROW(m_block->encode(m_blockData), TooMuchGasUsed);

        /// construct invalid block format
        bytes result_bytes = FakeInvalidBlockData(1, size);
        BOOST_CHECK_THROW(m_block->decode(ref(result_bytes)), InvalidBlockFormat);

        result_bytes = FakeInvalidBlockData(2, size);
        BOOST_CHECK_THROW(m_block->decode(ref(result_bytes)), InvalidBlockFormat);
    }

    /// fake block header
    void FakeBlockHeader(uint64_t blockNumber)
    {
        m_blockHeader.setParentHash(crypto::Hash("parent"));
        m_blockHeader.setRoots(crypto::Hash("transactionRoot"), crypto::Hash("receiptRoot"),
            crypto::Hash("stateRoot"));
        m_blockHeader.setLogBloom(LogBloom(0));
        m_blockHeader.setNumber(blockNumber);
        m_blockHeader.setGasLimit(u256(3000000));
        m_blockHeader.setGasUsed(u256(100000));
        uint64_t current_time = 100000;  // utcTime();
        m_blockHeader.setTimestamp(current_time);
        m_blockHeader.appendExtraDataArray(jonStringToBytes("0x1020"));
        m_blockHeader.setSealer(u256(12));
        std::vector<h512> sealer_list;
        for (unsigned int i = 0; i < 13; i++)
        {
            /// sealer_list.push_back(toPublic(Secret(h256(i))));
            sealer_list.push_back(m_keyPair.pub());
        }
        m_blockHeader.setSealerList(sealer_list);
    }

    /// fake sig list
    void FakeSigList(size_t size)
    {
        /// set sig list
        h256 block_hash;
        m_sigList->clear();
        for (size_t i = 0; i < size; i++)
        {
            block_hash = m_blockHeader.hash();
            auto sig = bcos::crypto::Sign(m_keyPair, block_hash);
            m_sigList->push_back(std::make_pair(u256(i), sig->asBytes()));
        }
    }

    /// fake transactions
    void FakeTransaction(size_t size)
    {
        RLPStream txs;
        txs.appendList(size);
        m_transaction->resize(size);
        fakeSingleTransaction();
        for (size_t i = 0; i < size; i++)
        {
            (*m_transaction)[i] = fakeSingleTransaction();
        }
        m_transactionData = TxsParallelParser::encode(m_transaction);
    }

    void FakeTransactionReceipt(size_t size)
    {
        m_transactionReceipt->resize(size);
        for (size_t i = 0; i < size; ++i)
        {
            (*m_transactionReceipt)[i] =
                std::make_shared<TransactionReceipt>(*m_singleTransactionReceipt);
        }
    }

    /// fake single transaction
    Transaction::Ptr fakeSingleTransaction()
    {
        u256 value = u256(100);
        u256 gas = u256(100000000);
        u256 gasPrice = u256(0);
        Address dst;
        std::string str = "test transaction";
        bytes data(str.begin(), str.end());
        auto fakedTx = std::make_shared<Transaction>(value, gasPrice, gas, dst, data, 2);
        m_singleTransaction = fakedTx;
        std::shared_ptr<crypto::Signature> sig =
            bcos::crypto::Sign(m_keyPair, m_singleTransaction->hash(WithoutSignature));
        /// update the signature of transaction
        m_singleTransaction->updateSignature(sig);
        return fakedTx;
    }

    void fakeSingleTransactionReceipt()
    {
        h256 root = h256("0x1024");
        u256 gasUsed = u256(10000);
        LogEntries logEntries = LogEntries();
        protocol::TransactionException status = protocol::TransactionException::Unknown;
        bytes outputBytes = bytes();
        Address address = toAddress(KeyPair::create().pub());
        m_singleTransactionReceipt = std::make_shared<TransactionReceipt>(
            root, gasUsed, logEntries, status, outputBytes, address);
    }

    shared_ptr<Transactions> fakeTransactions(size_t _num, int64_t _currentBlockNumber)
    {
        std::srand(utcTime());
        shared_ptr<Transactions> txs = make_shared<Transactions>();
        for (size_t i = 0; i < _num; ++i)
        {
            /// Transaction tx(ref(c_txBytes), CheckTransaction::Everything);
            u256 value = u256(100);
            u256 gas = u256(100000000);
            u256 gasPrice = u256(0);
            Address dst = toAddress(KeyPair::create().pub());
            std::string str = "test transaction";
            bytes data(str.begin(), str.end());
            Transaction::Ptr tx = std::make_shared<Transaction>(value, gasPrice, gas, dst, data);
            KeyPair sigKeyPair = KeyPair::create();
            tx->setNonce(tx->nonce() + utcTime() + m_nonceBase);
            tx->setBlockLimit(u256(_currentBlockNumber) + c_maxBlockLimit);
            tx->setRpcTx(true);
            std::shared_ptr<crypto::Signature> sig =
                bcos::crypto::Sign(sigKeyPair.secret(), tx->hash(WithoutSignature));
            /// update the signature of transaction
            tx->updateSignature(sig);
            // std::pair<h256, Address> ret = txPool->submit(tx);
            txs->emplace_back(tx);
            m_nonceBase++;
        }
        return txs;
    }

    std::shared_ptr<Block> getBlock() { return m_block; }
    BlockHeader& getBlockHeader() { return m_blockHeader; }
    bytes& getBlockHeaderData() { return m_blockHeaderData; }
    bytes& getBlockData() { return m_blockData; }

public:
    KeyPair m_keyPair;
    std::shared_ptr<Block> m_block;
    BlockHeader m_blockHeader;
    std::shared_ptr<Transactions> m_transaction;
    Transaction::Ptr m_singleTransaction;
    std::shared_ptr<TransactionReceipts> m_transactionReceipt;
    TransactionReceipt::Ptr m_singleTransactionReceipt = std::make_shared<TransactionReceipt>();
    std::shared_ptr<std::vector<std::pair<u256, std::vector<unsigned char>>>> m_sigList;
    bytes m_blockHeaderData;
    bytes m_blockData;
    bytes m_transactionData;
    size_t m_nonceBase = 0;
    const u256 c_maxBlockLimit = u256(1000);
};

}  // namespace test
}  // namespace bcos
