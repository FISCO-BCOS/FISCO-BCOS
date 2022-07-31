#pragma once

#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/protocol/Protocol.h>
#include <future>
#include <random>

namespace bcos::p2p
{
class P2PClientImpl
{
public:
    P2PClientImpl(bcos::front::FrontServiceInterface::Ptr front,
        bcos::gateway::GatewayInterface::Ptr gateway, bcos::crypto::KeyFactoryImpl::Ptr keyFactory)
      : m_front(std::move(front)),
        m_gateway(std::move(gateway)),
        m_keyFactory(std::move(keyFactory)),
        m_rng(std::random_device{}())
    {}

    bcos::bytes sendMessageByNodeID(
        int moduleID, crypto::NodeIDPtr nodeID, bcos::bytesConstRef buffer)
    {
        std::promise<std::tuple<bcos::Error::Ptr, bcos::bytes>> promise;
        m_front->asyncSendMessageByNodeID(bcos::protocol::LIGHTNODE_GETBLOCK, std::move(nodeID),
            buffer, 0,
            [&promise](Error::Ptr _error, bcos::crypto::NodeIDPtr, bytesConstRef _data,
                const std::string&, front::ResponseFunc) {
                promise.set_value(
                    std::make_tuple(std::move(_error), bcos::bytes(_data.begin(), _data.end())));
            });

        auto future = promise.get_future();
        auto [error, responseBuffer] = future.get();
        if (error)
            BOOST_THROW_EXCEPTION(*error);

        return responseBuffer;
    }

    crypto::NodeIDPtr randomSelectNode()
    {
        std::promise<std::tuple<bcos::Error::Ptr, std::string>> promise;
        m_gateway->asyncGetPeers([this, &promise](Error::Ptr error, gateway::GatewayInfo::Ptr,
                                     gateway::GatewayInfosPtr peerGatewayInfos) {
            auto groups = peerGatewayInfos->at(0);
            auto nodeIDInfo = groups->nodeIDInfo();
            auto it = nodeIDInfo.find("group0");
            if (it != nodeIDInfo.end())
            {
                auto& nodeIDs = it->second;
                if (nodeIDs.empty())
                {
                    promise.set_value(std::make_tuple(std::move(error), std::string{}));
                    return;
                }

                std::uniform_int_distribution<size_t> distribution{0u, nodeIDs.size()};
                auto nodeIDIt = nodeIDs.begin();
                auto step = distribution(m_rng);
                for (size_t i = 0; i < step; ++i)
                    ++nodeIDIt;
                promise.set_value(std::make_tuple(std::move(error), *nodeIDIt));
            }
            else
            {
                promise.set_value(std::make_tuple(std::move(error), std::string{}));
            }
        });

        auto [error, nodeIDStr] = promise.get_future().get();
        if (error)
            BOOST_THROW_EXCEPTION(*error);

        if (nodeIDStr.empty())
            BOOST_THROW_EXCEPTION(std::runtime_error{"No node available"});

        bcos::bytes nodeIDBin;
        boost::algorithm::unhex(nodeIDStr.begin(), nodeIDStr.end(), std::back_inserter(nodeIDBin));
        auto nodeID = m_keyFactory->createKey(nodeIDBin);
        return nodeID;
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::crypto::KeyFactoryImpl::Ptr m_keyFactory;
    std::mt19937 m_rng;
};
}  // namespace bcos::p2p