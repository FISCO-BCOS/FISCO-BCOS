#pragma once

#include "bcos-crypto/interfaces/crypto/KeyInterface.h"
#include "bcos-lightnode/Log.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-task/Task.h>
#include <range/v3/view/single.hpp>
#include <random>

namespace bcos::p2p
{

DERIVE_BCOS_EXCEPTION(NoNodeAvailable);
class P2PClientImpl
{
public:
    P2PClientImpl(bcos::front::FrontServiceInterface::Ptr front,
        bcos::gateway::GatewayInterface::Ptr gateway, bcos::crypto::KeyFactoryImpl::Ptr keyFactory,
        std::string groupID)
      : m_front(std::move(front)),
        m_gateway(std::move(gateway)),
        m_keyFactory(std::move(keyFactory)),
        m_groupID(std::move(groupID)),
        m_rng(std::random_device{}())
    {}

    task::Task<void> sendMessageByNodeID(int moduleID, crypto::NodeIDPtr nodeID,
        bcos::concepts::serialize::Serializable auto const& request,
        bcos::concepts::serialize::Serializable auto& response)
    {
        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        LIGHTNODE_LOG(DEBUG) << "P2P client send message: " << moduleID << " | "
                             << nodeID->hex() << " | " << requestBuffer.size();
        auto result = co_await m_front->sendMessageByNodeID(moduleID, nodeID,
            ::ranges::views::single(bcos::ref(requestBuffer)), 30000);
        LIGHTNODE_LOG(DEBUG) << "P2P client receive message: " << moduleID << " | "
                             << nodeID->hex() << " | " << result.payload.size() << " | "
                             << (result.error ? result.error->errorCode() : 0) << " | "
                             << (result.error ? result.error->errorMessage() : "");
        if (result.error)
        {
            BOOST_THROW_EXCEPTION(*result.error);
        }
        bcos::concepts::serialize::decode(bcos::ref(result.payload), response);
        LIGHTNODE_LOG(DEBUG) << LOG_DESC("P2P client receive message success: ")
                             << LOG_KV("data size", result.payload.size());
    }

    task::Task<crypto::NodeIDPtr> randomSelectNode()
    {
        auto [error, localGatewayInfo, peers] = co_await m_gateway->getPeers();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        std::string nodeID;
        if (!peers->empty())
        {
            std::set<std::string> nodeIDs;
            for (const auto& peerGatewayInfo : *peers)
            {
                auto nodeIDInfo = peerGatewayInfo->nodeIDInfo();
                auto nodeInfo = nodeIDInfo.find(m_groupID);

                if (nodeInfo != nodeIDInfo.end() && !nodeInfo->second.empty())
                {
                    for (auto& it : nodeInfo->second)
                    {
                        if (it.second == bcos::protocol::NodeType::CONSENSUS_NODE ||
                            it.second == bcos::protocol::NodeType::OBSERVER_NODE)
                        {
                            nodeIDs.insert(it.first);
                            LIGHTNODE_LOG(TRACE)
                                << LOG_KV("NodeID:", it.first)
                                << LOG_KV("nodeType:", it.second);
                        }
                    }
                }
            }

            if (!nodeIDs.empty())
            {
                std::uniform_int_distribution<size_t> distribution{0U, nodeIDs.size() - 1};
                auto nodeIDIt = nodeIDs.begin();
                auto step = distribution(m_rng);
                for (size_t i = 0; i < step; ++i)
                {
                    ++nodeIDIt;
                }

                nodeID = *nodeIDIt;
            }
        }

        if (nodeID.empty())
        {
            LIGHTNODE_LOG(INFO) << LOG_DESC(
                "randomSelectNode failed, nodeID is empty, no node available");
            BOOST_THROW_EXCEPTION(
                NoNodeAvailable{} << errinfo_comment{
                    "no node available, please check the node and network status"});
        }

        bcos::bytes nodeIDBin;
        boost::algorithm::unhex(nodeID.begin(), nodeID.end(), std::back_inserter(nodeIDBin));
        auto nodeIDPtr = m_keyFactory->createKey(nodeIDBin);
        co_return nodeIDPtr;
    }

    task::Task<bcos::crypto::NodeIDs> getAllNodeID()
    {
        auto [error, localGatewayInfo, peers] = co_await m_gateway->getPeers();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        std::set<std::string> nodeIDList;
        if (!peers->empty())
        {
            std::set<std::string> nodeIDs;
            for (const auto& peerGatewayInfo : *peers)
            {
                auto nodeIDInfo = peerGatewayInfo->nodeIDInfo();
                auto nodeInfo = nodeIDInfo.find(m_groupID);

                if (nodeInfo != nodeIDInfo.end() && !nodeInfo->second.empty())
                {
                    for (auto& it : nodeInfo->second)
                    {
                        if (it.second == bcos::protocol::NodeType::CONSENSUS_NODE ||
                            it.second == bcos::protocol::NodeType::OBSERVER_NODE)
                        {
                            nodeIDs.insert(it.first);
                            LIGHTNODE_LOG(TRACE)
                                << LOG_KV("NodeID:", it.first)
                                << LOG_KV("nodeType:", it.second);
                        }
                    }
                }
            }
            if (!nodeIDs.empty())
            {
                nodeIDList = std::move(nodeIDs);
            }
        }

        LIGHTNODE_LOG(DEBUG) << LOG_KV("nodeIDList size", nodeIDList.size());
        bcos::crypto::NodeIDs nodeIDs;
        for (const auto& nodeID : nodeIDList)
        {
            bcos::bytes nodeIDBin;
            boost::algorithm::unhex(nodeID.begin(), nodeID.end(), std::back_inserter(nodeIDBin));
            LIGHTNODE_LOG(DEBUG) << LOG_KV("nodeID", nodeID);
            auto nodeIDPtr = m_keyFactory->createKey(nodeIDBin);
            nodeIDs.push_back(nodeIDPtr);
        }
        if (nodeIDs.empty())
        {
            BOOST_THROW_EXCEPTION(
                NoNodeAvailable{} << errinfo_comment{
                    "no node available, please check the node and network status"});
        }
        co_return nodeIDs;
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::crypto::KeyFactoryImpl::Ptr m_keyFactory;
    std::string m_groupID;
    std::mt19937 m_rng;
};
}  // namespace bcos::p2p