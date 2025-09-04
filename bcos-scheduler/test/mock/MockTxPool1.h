#pragma once

#include "bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-scheduler/src/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <boost/test/unit_test.hpp>


using namespace std;
using namespace bcos::crypto;
namespace bcos::test
{
class MockTxPool1 : public txpool::TxPoolInterface
{
public:
    using Ptr = std::shared_ptr<MockTxPool1>();
    void start() override {}
    void stop() override {}
    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction, bool /*waitForReceipt*/) override
    {
        co_return nullptr;
    }

    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t) override
    {
        return {};
    }
    void asyncMarkTxs(const bcos::crypto::HashList&, bool, bcos::protocol::BlockNumber,
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)>) override
    {}
    void asyncVerifyBlock(bcos::crypto::PublicPtr, protocol::Block::ConstPtr,
        std::function<void(Error::Ptr, bool)>) override
    {}
    void asyncNotifyBlockResult(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>) override
    {}
    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr, std::string const&, bcos::crypto::NodeIDPtr,
        bytesConstRef, std::function<void(Error::Ptr _error)>) override
    {}
    void notifyConsensusNodeList(
        bcos::consensus::ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    void notifyObserverNodeList(
        bcos::consensus::ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    void asyncGetPendingTransactionSize(std::function<void(Error::Ptr, uint64_t)>) override {}
    void asyncResetTxPool(std::function<void(Error::Ptr)>) override {}

    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _onBlockFilled)
        override
    {
        BOOST_CHECK_GT(_txsHash->size(), 0);
        SCHEDULER_LOG(DEBUG) << LOG_KV("txHashes size", _txsHash->size())
                             << LOG_KV("map Size", hash2Transaction.size());
        auto transactions = std::make_shared<bcos::protocol::ConstTransactions>();
        for (auto& hash : *_txsHash)
        {
            auto it = hash2Transaction.find(hash);
            if (it != hash2Transaction.end())
            {
                transactions->push_back(it->second);
                SCHEDULER_LOG(DEBUG) << LOG_KV("hash", hash) << LOG_KV("tx", it->second);
            }
            else
            {
                transactions->push_back(nullptr);
            }
        }
        _onBlockFilled(nullptr, std::move(transactions));
    }

    void notifyConnectedNodes(
        const bcos::crypto::NodeIDSet&, std::function<void(std::shared_ptr<bcos::Error>)>) override
    {}

    // protocol::HashList generateTransaction(std::size_t number)
    // {
    //     auto txHashes = std::make_shared<protocol::HashList>();
    //     for (size_t i = 0; i < number; ++i)
    //     {
    //         hashImpl = std::make_shared<Keccak256>();
    //         assert(hashImpl);
    //         signatureImpl = std::make_shared<Sep256k1Crypto>();
    //         assert(signatureImpl);
    //         cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    //         keyPair = cryptoSuite->signatureImpl()->generateKeyPair();

    //         // Generate fakeTransaction
    //         auto tx = fakeTransaction(cryptoSuite, keyPair, "", "", std::to_string(101), 100001,
    //         "1", "1"); auto hash = tx->hash(); hash2Transaction.emplace(hash, tx);
    //         txHashes.emplace_back(hash);
    //     }
    //     return txHashes;
    // }

public:
    std::map<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr> hash2Transaction;
    bcos::crypto::Hash::Ptr hashImpl;
    bcos::crypto::SignatureCrypto::Ptr signatureImpl;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcos::crypto::KeyPairInterface::Ptr keyPair;
};
}  // namespace bcos::test