#pragma once

#include "../groupmgr/NodeService.h"
#include <bcos-tars-protocol/tars/RPC.h>

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

private:
    NodeService::Ptr m_node;
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