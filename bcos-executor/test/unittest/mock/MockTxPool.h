#pragma once

#include <bcos-framework/txpool/TxPoolInterface.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
class MockTxPool : public txpool::TxPoolInterface
{
public:
    void start() override {}
    void stop() override {}
    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction, bool /*waitForReceipt*/) override
    {
        co_return nullptr;
    }
    std::tuple<bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr> sealTxs(
        uint64_t, bcos::txpool::TxsHashSetPtr) override
    {
        return {};
    }
    void asyncMarkTxs(const bcos::crypto::HashList&, bool, bcos::protocol::BlockNumber,
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)>) override
    {}
    void asyncVerifyBlock(bcos::crypto::PublicPtr, bytesConstRef const&,
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
        auto transactions = std::make_shared<bcos::protocol::ConstTransactions>();
        for (auto& hash : *_txsHash)
        {
            auto it = hash2Transaction.find(hash);
            if (it != hash2Transaction.end())
            {
                transactions->push_back(it->second);
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

    std::map<bcos::crypto::HashType, bcos::protocol::Transaction::ConstPtr> hash2Transaction;
};
}  // namespace bcos::test