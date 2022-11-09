#pragma once

#include "bcos-crypto/interfaces/crypto/KeyFactory.h"
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/front/Front.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <variant>

namespace bcos::front
{

#define FRONT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("Front")

// clang-format off
struct NoNodeAvailable: public bcos::error::Exception {};
// clang-format on

class FrontImpl : public bcos::concepts::front::FrontBase<FrontImpl>
{
public:
    FrontImpl(bcos::front::FrontServiceInterface::Ptr front,
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

        crypto::NodeIDPtr nodeIDPtr;
        using NodeIDType = std::decay_t<decltype(nodeID)>;
        if constexpr (std::is_same_v<NodeIDType, crypto::NodeIDPtr>)
        {
            nodeIDPtr = nodeID;
        }
        else if constexpr (concepts::bytebuffer::ByteBuffer<NodeIDType>)
        {
            auto nodeIDBin = bcos::fromHex(nodeID);
            nodeIDPtr = m_keyFactory->createKey(nodeIDBin);
        }
        else
        {
            static_assert(!sizeof(nodeID), "Unspported nodeID type!");
        }

        using ResponseType = std::remove_cvref_t<decltype(response)>;
        struct Awaitable
        {
            constexpr bool await_ready() const { return false; }

            void await_suspend(CO_STD::coroutine_handle<task::Task<void>::promise_type> handle)
            {
                FRONT_LOG(DEBUG) << "P2P client send message: " << m_moduleID << " | "
                                 << m_nodeID->hex() << " | " << m_requestBuffer.size();
                bcos::concepts::getRef(m_front).asyncSendMessageByNodeID(m_moduleID, m_nodeID,
                    bcos::ref(m_requestBuffer), DEFAULT_TIMEOUT,
                    [m_handle = handle, this](Error::Ptr error, const bcos::crypto::NodeIDPtr&,
                        bytesConstRef data, const std::string&,
                        const front::ResponseFunc&) mutable {
                        FRONT_LOG(DEBUG) << "P2P client receive message: " << m_moduleID << " | "
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
            ModuleID m_moduleID = 0;
            crypto::NodeIDPtr m_nodeID;
            bcos::bytes m_requestBuffer;

            // Response params
            Error::Ptr m_error;
            ResponseType& m_response;
        };

        auto awaitable = Awaitable{.m_front = m_front,
            .m_moduleID = moduleID,
            .m_nodeID = std::move(nodeIDPtr),
            .m_requestBuffer = std::move(requestBuffer),
            .m_response = response};
        co_await awaitable;
    }

    task::Task<void> impl_broadcastMessage(NodeType type, ModuleID moduleID,
        bcos::concepts::serialize::Serializable auto const& request)
    {}

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string m_groupID;

    constexpr static uint32_t DEFAULT_TIMEOUT = 3000;
};
}  // namespace bcos::front