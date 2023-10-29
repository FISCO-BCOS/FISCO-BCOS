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
        static size_t hash(const tars::CurrentPtr& key);
        static bool equal(const tars::CurrentPtr& lhs, const tars::CurrentPtr& rhs);
    };

    NodeService::Ptr node;
    tbb::concurrent_hash_map<tars::CurrentPtr, std::vector<std::string>, PtrHash> sessions;
};

class RPCServer : public bcostars::RPC
{
public:
    RPCServer(Params& params) : m_params(params) {}

    void initialize() override;
    void destroy() override;

    bcostars::Error handshake(
        const std::vector<std::string>& topics, tars::TarsCurrentPtr current) override;
    bcostars::Error call(const bcostars::Transaction& request,
        bcostars::TransactionReceipt& response, tars::TarsCurrentPtr current) override;
    bcostars::Error sendTransaction(const bcostars::Transaction& request,
        bcostars::TransactionReceipt& response, tars::TarsCurrentPtr current) override;
    bcostars::Error blockNumber(long& number, tars::TarsCurrentPtr current) override;

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
    void pushAMOPMessage(std::string_view topic, bcos::bytesConstRef message);

    static std::string generateTarsConfig(std::string_view host, uint16_t port, size_t threadCount);

private:
    Params m_params;
};
}  // namespace bcos::rpc