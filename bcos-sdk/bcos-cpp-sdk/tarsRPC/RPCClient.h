#pragma once

#include "bcos-rpc/jsonrpc/Common.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-tars-protocol/tars/RPC.h>
#include <oneapi/tbb/concurrent_queue.h>
#include <servant/Communicator.h>
#include <tbb/concurrent_queue.h>
#include <any>

namespace bcos::sdk
{

class CompletionQueue
{
    friend class RPCClient;

private:
    tbb::concurrent_bounded_queue<std::any> m_asyncQueue;

public:
    // Wait for async operation complete, thread safe
    std::any wait();
    std::optional<std::any> tryWait();
};

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

public:
    RPCClient(const std::string& connectionString);

    std::future<bcos::protocol::TransactionReceipt::Ptr> sendTransaction(
        const bcos::protocol::Transaction& transaction, CompletionQueue* completionQueue = nullptr,
        std::any tag = {});
};
}  // namespace bcos::sdk