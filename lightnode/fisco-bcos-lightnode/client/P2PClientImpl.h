#pragma once

#include "bcos-concepts/Exception.h"
#include "bcos-crypto/interfaces/crypto/KeyInterface.h"
#include "bcos-lightnode/Log.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-task/Task.h>
#include <future>
#include <random>

namespace bcos::p2p
{

// clang-format off
struct NoNodeAvailable: public bcos::exception::Exception {};
// clang-format on

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

    task::Task<void> sendMessageByNodeID(int moduleID, crypto::NodeIDPtr nodeID,
        bcos::concepts::serialize::Serializable auto const& request,
        bcos::concepts::serialize::Serializable auto& response)
    {
        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        using ResponseType = std::remove_cvref_t<decltype(response)>;
        struct Awaitable
        {
            Awaitable(bcos::front::FrontServiceInterface::Ptr& front, int moduleID,
                crypto::NodeIDPtr nodeID, bcos::bytes buffer, ResponseType& response)
              : m_front(front),
                m_moduleID(moduleID),
                m_nodeID(std::move(nodeID)),
                m_requestBuffer(std::move(buffer)),
                m_response(response)
            {}
            constexpr bool await_ready() const { return false; }

            void await_suspend(CO_STD::coroutine_handle<task::Task<void>::promise_type> handle)
            {
                LIGHTNODE_LOG(DEBUG) << "P2P client send message: " << m_moduleID << " | "
                                     << m_nodeID << " | " << m_requestBuffer.size();
                bcos::concepts::getRef(m_front).asyncSendMessageByNodeID(m_moduleID, m_nodeID,
                    bcos::ref(m_requestBuffer), 0,
                    [m_handle = std::move(handle), this](Error::Ptr error, bcos::crypto::NodeIDPtr,
                        bytesConstRef data, const std::string&, front::ResponseFunc) mutable {
                        LIGHTNODE_LOG(DEBUG) << "P2P client receive message: " << m_moduleID
                                             << " | " << m_nodeID << " | " << data.size();
                        if (!error)
                        {
                            bcos::concepts::serialize::decode(data, m_response);
                        }
                        else
                        {
                            m_error = std::move(error);
                        }

                        m_handle.resume();
                    });
            }

            constexpr void await_resume() const
            {
                if (m_error)
                {
                    BOOST_THROW_EXCEPTION(*m_error);
                }
            }

            // Request params
            bcos::front::FrontServiceInterface::Ptr& m_front;
            int m_moduleID;
            crypto::NodeIDPtr m_nodeID;
            bcos::bytes m_requestBuffer;

            // Response params
            Error::Ptr m_error;
            ResponseType& m_response;
        };

        co_await Awaitable(m_front, moduleID, nodeID, std::move(requestBuffer), response);
    }

    task::Task<crypto::NodeIDPtr> randomSelectNode()
    {
        struct Awaitable
        {
            Awaitable(bcos::gateway::GatewayInterface::Ptr& gateway, std::mt19937& rng)
              : m_gateway(gateway), m_rng(rng)
            {}

            constexpr bool await_ready() const noexcept { return false; }
            void await_suspend(CO_STD::coroutine_handle<> handle)
            {
                bcos::concepts::getRef(m_gateway).asyncGetPeers(
                    [this, m_handle = std::move(handle)](Error::Ptr error,
                        gateway::GatewayInfo::Ptr,
                        gateway::GatewayInfosPtr peerGatewayInfos) mutable {
                        if (error)
                        {
                            m_error = std::move(error);
                        }
                        else
                        {
                            if (!peerGatewayInfos->empty())
                            {
                                auto groups = peerGatewayInfos->at(0);
                                auto nodeIDInfo = groups->nodeIDInfo();
                                auto it = nodeIDInfo.find("group0");
                                if (it != nodeIDInfo.end())
                                {
                                    auto& nodeIDs = it->second;
                                    if (!nodeIDs.empty())
                                    {
                                        std::uniform_int_distribution<size_t> distribution{
                                            0U, nodeIDs.size() - 1};
                                        auto nodeIDIt = nodeIDs.begin();
                                        auto step = distribution(m_rng);
                                        for (size_t i = 0; i < step; ++i)
                                            ++nodeIDIt;

                                        m_nodeID = *nodeIDIt;
                                    }
                                }
                            }
                        }

                        m_handle.resume();
                    });
            }
            std::string await_resume()
            {
                if (m_error)
                {
                    BOOST_THROW_EXCEPTION(*(m_error));
                }
                return std::move(m_nodeID);
            }

            bcos::gateway::GatewayInterface::Ptr& m_gateway;
            std::mt19937& m_rng;

            Error::Ptr m_error;
            std::string m_nodeID;
        };

        auto nodeID = co_await Awaitable(m_gateway, m_rng);
        if (nodeID.empty())
        {
            BOOST_THROW_EXCEPTION(NoNodeAvailable{});
        }

        bcos::bytes nodeIDBin;
        boost::algorithm::unhex(nodeID.begin(), nodeID.end(), std::back_inserter(nodeIDBin));
        auto nodeIDPtr = m_keyFactory->createKey(nodeIDBin);
        co_return nodeIDPtr;
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::crypto::KeyFactoryImpl::Ptr m_keyFactory;
    std::mt19937 m_rng;
};
}  // namespace bcos::p2p