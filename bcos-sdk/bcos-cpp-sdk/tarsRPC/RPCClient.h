#pragma once

#include "Handle.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-tars-protocol/tars/RPC.h"

namespace bcos::sdk
{

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

    static void onMessage(tars::ReqMessagePtr message);

public:
    RPCClient(const std::string& connectionString);
    bcostars::RPCPrx& rpcProxy() { return m_rpcProxy; }
};

class SendTransaction : public bcos::sdk::Handle<bcos::protocol::TransactionReceipt::Ptr>
{
public:
    SendTransaction(RPCClient& rpcClient);
    SendTransaction& send(const bcos::protocol::Transaction& transaction);
};

class BlockNumber : public bcos::sdk::Handle<long>
{
public:
    BlockNumber(RPCClient& rpcClient);
    BlockNumber& send();
};

}  // namespace bcos::sdk