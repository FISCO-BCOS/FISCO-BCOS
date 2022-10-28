#pragma once

#include "bcos-crypto/interfaces/crypto/KeyFactory.h"
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/p2p/P2P.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <variant>

namespace bcos::p2p
{

#define P2P_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LIGHTNODE")

// clang-format off
struct NoNodeAvailable: public bcos::error::Exception {};
// clang-format on

class P2PImpl : public bcos::concepts::p2p::P2P<P2PImpl>
{
public:
    P2PImpl(bcos::front::FrontServiceInterface::Ptr front,
        bcos::gateway::GatewayInterface::Ptr gateway, bcos::crypto::KeyFactory::Ptr keyFactory,
        std::string groupID)
      : m_front(std::move(front)),
        m_gateway(std::move(gateway)),
        m_keyFactory(std::move(keyFactory)),
        m_groupID(std::move(groupID))
    {}

    task::Task<std::set<std::string>> nodeIDs()
    {
        struct Awaitable
        {
            Awaitable(bcos::gateway::GatewayInterface::Ptr& gateway, std::string& groupID)
              : m_gateway(gateway), m_groupID(groupID)
            {}

            constexpr bool await_ready() const noexcept { return false; }
            void await_suspend(CO_STD::coroutine_handle<> handle)
            {
                bcos::concepts::getRef(m_gateway).asyncGetPeers(
                    [this, m_handle = handle](Error::Ptr error, const gateway::GatewayInfo::Ptr&,
                        const gateway::GatewayInfosPtr& peerGatewayInfos) mutable {
                        if (error)
                        {
                            m_result = std::move(error);
                        }
                        else
                        {
                            if (!peerGatewayInfos->empty())
                            {
                                for (const auto& peerGatewayInfo : *peerGatewayInfos)
                                {
                                    auto nodeIDInfo = peerGatewayInfo->nodeIDInfo();
                                    auto it = nodeIDInfo.find(m_groupID);

                                    if (it != nodeIDInfo.end() && !it->second.empty())
                                    {
                                        m_result = std::move(it->second);
                                        break;
                                    }
                                }
                            }
                        }

                        m_handle.resume();
                    });
            }
            std::set<std::string> await_resume()
            {
                if (std::holds_alternative<Error::Ptr>(m_result))
                {
                    BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
                }

                if (std::holds_alternative<std::monostate>(m_result))
                {
                    BOOST_THROW_EXCEPTION(NoNodeAvailable{});
                }

                return std::move(std::get<std::set<std::string>>(m_result));
            }

            bcos::gateway::GatewayInterface::Ptr& m_gateway;
            std::string& m_groupID;

            std::variant<std::monostate, Error::Ptr, std::set<std::string>> m_result;
        };

        auto awaitable = Awaitable(m_gateway, m_groupID);
        co_return co_await awaitable;
    }

    task::Task<void> impl_sendMessageByNodeID(int moduleID, auto const& nodeID,
        concepts::serialize::Serializable auto const& request,
        concepts::serialize::Serializable auto& response)
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
                P2P_LOG(DEBUG) << "P2P client send message: " << m_moduleID << " | "
                               << m_nodeID->hex() << " | " << m_requestBuffer.size();
                bcos::concepts::getRef(m_front).asyncSendMessageByNodeID(m_moduleID, m_nodeID,
                    bcos::ref(m_requestBuffer), 30000,
                    [m_handle = std::move(handle), this](Error::Ptr error, bcos::crypto::NodeIDPtr,
                        bytesConstRef data, const std::string&, front::ResponseFunc) mutable {
                        P2P_LOG(DEBUG) << "P2P client receive message: " << m_moduleID << " | "
                                       << m_nodeID->hex() << " | " << data.size() << " | "
                                       << (error ? error->errorCode() : 0) << " | "
                                       << (error ? error->errorMessage() : "");
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

        auto awaitable = Awaitable(m_front, moduleID, nodeID, std::move(requestBuffer), response);
        co_await awaitable;
    }

    task::Task<void> impl_broadcastMessage(
        uint16_t type, int moduleID, bcos::concepts::serialize::Serializable auto const& request)
    {}

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string m_groupID;
};
}  // namespace bcos::p2p