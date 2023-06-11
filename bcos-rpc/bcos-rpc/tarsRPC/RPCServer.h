#pragma once

#include "../groupmgr/NodeService.h"
#include <bcos-tars-protocol/tars/RPC.h>
#include <tbb/concurrent_hash_map.h>

#include <utility>

namespace bcos::rpc
{

struct Params
{
    struct PtrHash
    {
        static size_t hash(const tars::CurrentPtr& key)
        {
            return std::hash<void*>{}((void*)key.get());
        }
        static bool equal(const tars::CurrentPtr& lhs, const tars::CurrentPtr& rhs)
        {
            return lhs == rhs;
        }
    };

    NodeService::Ptr node;
    tbb::concurrent_hash_map<tars::CurrentPtr, std::monostate, PtrHash> sessions;
};

class RPCServer : public bcostars::RPC
{
public:
    RPCServer(Params& params) : m_params(params) {}

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
    Params& m_params;
};

class RPCApplication : public tars::Application
{
public:
    RPCApplication(NodeService::Ptr node) { m_params.node = std::move(node); }

    RPCApplication& operator=(const RPCApplication&) = delete;
    RPCApplication(const RPCApplication&) = delete;
    RPCApplication& operator=(RPCApplication&&) = delete;
    RPCApplication(RPCApplication&&) = delete;
    ~RPCApplication() override = default;

    void initialize() override;
    void destroyApp() override;

    void pushBlockNumber(long blockNumber);

private:
    Params m_params;
};
}  // namespace bcos::rpc