#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/RPC.h"
#include "detail/RPCHandle.h"

namespace bcos::sdk
{

struct Config
{
    std::string connectionString;
    long sendQueueSize = 0;
    int timeoutMs = 60000;
};

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

    static void onMessage(tars::ReqMessagePtr message);

public:
    RPCClient(Config const& config);
    bcostars::RPCPrx& rpcProxy();

    static std::string toConnectionString(const std::vector<std::string>& hostAndPorts);
    std::string generateNonce();
};

class SendTransaction : public bcos::sdk::RPCHandle<bcos::protocol::TransactionReceipt::Ptr>
{
public:
    SendTransaction(RPCClient& rpcClient);
    SendTransaction& send(const bcos::protocol::Transaction& transaction);
};

class Call : public bcos::sdk::RPCHandle<protocol::TransactionReceipt::Ptr>
{
public:
    Call(RPCClient& rpcClient);
    Call& send(const protocol::Transaction& transaction);
};

class BlockNumber : public bcos::sdk::RPCHandle<long>
{
public:
    BlockNumber(RPCClient& rpcClient);
    BlockNumber& send();
};

}  // namespace bcos::sdk