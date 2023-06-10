#pragma once

#include "../groupmgr/NodeService.h"
#include <bcos-tars-protocol/tars/RPC.h>
#include <servant/Global.h>
#include <tbb/concurrent_hash_map.h>

namespace bcos::rpc
{
class RPCServer : public bcostars::RPC
{
public:
    RPCServer(NodeService::Ptr node) : m_node(std::move(node)) {}

    void initialize() override;
    void destroy() override;

    bcostars::Error call(const bcostars::Transaction& request,
        bcostars::TransactionReceipt& response, tars::TarsCurrentPtr current) override;
    bcostars::Error sendTransaction(const bcostars::Transaction& request,
        bcostars::TransactionReceipt& response, tars::TarsCurrentPtr current) override;
    bcostars::Error blockNumber(long& number, tars::TarsCurrentPtr current) override;

    int onDispatch(tars::CurrentPtr current, std::vector<char>& buffer) override;
    int doClose(tars::CurrentPtr current) override;

private:
    NodeService::Ptr m_node;
    tbb::concurrent_hash_map<tars::CurrentPtr, std::monostate> m_connections;
};

class RPCApplication : public tars::Application
{
public:
    RPCApplication(NodeService::Ptr node) : m_node(std::move(node)) {}

    RPCApplication& operator=(const RPCApplication&) = delete;
    RPCApplication(const RPCApplication&) = delete;
    RPCApplication& operator=(RPCApplication&&) = delete;
    RPCApplication(RPCApplication&&) = delete;
    ~RPCApplication() override = default;

    void initialize() override;
    void destroyApp() override;

private:
    NodeService::Ptr m_node;
};
}  // namespace bcos::rpc