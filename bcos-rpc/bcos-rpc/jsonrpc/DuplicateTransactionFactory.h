//
// Created by Jimmy Shi on 2022/6/11.
//
#pragma once
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/ThreadPool.h>

namespace bcos
{
namespace rpc
{
class DuplicateTransactionFactory
{
public:
    static void multiBuild(bcos::protocol::TransactionFactory::Ptr transactionFactory,
        bytes const& seedTxData, bcos::crypto::KeyPairInterface::Ptr keyPair, int64_t num,
        std::function<void(bcos::protocol::Transaction::Ptr)> onTxBuild);

    static void asyncMultiBuild(bcos::protocol::TransactionFactory::Ptr transactionFactory,
        bytes const& seedTxData, bcos::crypto::KeyPairInterface::Ptr keyPair, int64_t num,
        std::function<void(bcos::protocol::Transaction::Ptr)> onTxBuild)
    {
        static ThreadPool pool("DuplicateTransactionFactory", std::thread::hardware_concurrency());
        pool.enqueue(
            [transactionFactory, seedTxData, keyPair, num, onTxBuild = std::move(onTxBuild)]() {
                multiBuild(transactionFactory, seedTxData, keyPair, num, std::move(onTxBuild));
            });
    }
};
}  // namespace rpc
}  // namespace bcos
