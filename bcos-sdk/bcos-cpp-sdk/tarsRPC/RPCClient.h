#pragma once

#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-tars-protocol/tars/RPC.h>
#include <servant/Communicator.h>

namespace bcos::sdk
{

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

public:
    RPCClient();
    using AsyncID = int64_t;

    std::future<bcos::protocol::TransactionReceipt::Ptr> sendTransaction(
        const bcos::protocol::Transaction& transaction, AsyncID asyncID = 0);

    // Wait for async operation complete
    std::vector<AsyncID> wait();
};
}  // namespace bcos::sdk