#include "AirTopicManager.h"

bcos::amop::LocalTopicManager::LocalTopicManager(
    std::string const& _rpcServiceName, bcos::gateway::P2PInterface::Ptr _network)
  : TopicManager(_rpcServiceName, std::move(_network))
{}

bcos::amop::LocalTopicManager::~LocalTopicManager() = default;

void bcos::amop::LocalTopicManager::setLocalClient(bcos::rpc::RPCInterface::Ptr _rpc)
{
    m_rpc = std::move(_rpc);
}

bcos::rpc::RPCInterface::Ptr bcos::amop::LocalTopicManager::createAndGetServiceByClient(
    std::string const&)
{
    return m_rpc;
}

void bcos::amop::LocalTopicManager::start() {}

void bcos::amop::LocalTopicManager::stop() {}