/** @file Service.h
 *  @author monan
 *  @modify first draft
 *  @date 20180910
 *  @author chaychen
 *  @modify realize encode and decode, add timeout, code format
 *  @date 20180911
 */

#pragma once
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-framework/interfaces/protocol/ProtocolInfoCodec.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/libp2p/P2PSession.h>

#include <map>
#include <memory>
#include <unordered_map>

namespace bcos
{
namespace gateway
{
class P2PMessage;
class Gateway;

class Service : public P2PInterface, public std::enable_shared_from_this<Service>
{
public:
    Service(std::shared_ptr<boostssl::ws::WsService> _wsService);
    virtual ~Service() { stop(); }

    using Ptr = std::shared_ptr<Service>;

    void start() override;
    void stop() override;

    virtual bool actived() { return m_run; }
    P2pID id() const override { return m_p2pID; }
    void setId(const P2pID& _nodeID) { m_p2pID = _nodeID; }

    virtual void onConnect(std::shared_ptr<boostssl::ws::WsSession> _session);
    virtual void onDisconnect(boostssl::ws::WsSession::Ptr p2pSession);

    std::shared_ptr<bcos::boostssl::MessageFace> sendMessageByNodeID(
        P2pID p2pID, std::shared_ptr<bcos::boostssl::MessageFace> message) override;
    void sendMessageBySession(
        int _packetType, bytesConstRef _payload, P2PSession::Ptr _p2pSession) override;
    void sendRespMessageBySession(bytesConstRef _payload,
        bcos::boostssl::MessageFace::Ptr _p2pMessage, P2PSession::Ptr _p2pSession) override;
    void asyncSendMessageByNodeID(P2pID p2pID, std::shared_ptr<bcos::boostssl::MessageFace> message,
        CallbackFuncWithSession callback,
        boostssl::ws::Options options = boostssl::ws::Options()) override;

    void asyncBroadcastMessage(std::shared_ptr<bcos::boostssl::MessageFace> message,
        boostssl::ws::Options options) override;

    virtual std::map<boostssl::NodeIPEndpoint, P2pID> staticNodes() { return m_staticNodes; }
    virtual void setStaticNodes(const std::set<boostssl::NodeIPEndpoint>& staticNodes)
    {
        for (const auto& endpoint : staticNodes)
        {
            m_staticNodes.insert(std::make_pair(endpoint, ""));
        }
    }

    void obtainHostAndPortFromString(
        std::string const& _endpointString, std::string& host, uint16_t& port);
    std::string obtainCommonNameFromSubject(std::string const& subject);

    P2pInfos sessionInfos() override;  ///< Only connected node

    P2pInfo nodeInfo();
    P2pInfo localP2pInfo() override;

    bool isConnected(P2pID const& p2pID) const override;

    std::shared_ptr<boostssl::ws::WsService> wsService() { return m_wsService; }

    std::shared_ptr<boostssl::MessageFaceFactory> messageFactory() override
    {
        return m_messageFactory;
    }
    virtual void setMessageFactory(std::shared_ptr<boostssl::MessageFaceFactory> _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory() { return m_keyFactory; }

    void setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory)
    {
        m_keyFactory = _keyFactory;
    }

    void updateUnconnectedEndpointToWservice();

    void updateStaticNodes(std::string const& _endPoint, P2pID const& p2pID);

    void registerDisconnectHandler(std::function<void(P2PSession::Ptr)> _handler)
    {
        m_disconnectionHandlers.push_back(_handler);
    }

    std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _p2pID) override
    {
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(_p2pID);
        if (it != m_sessions.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void asyncSendMessageByP2PNodeID(int16_t _type, P2pID _dstNodeID, bytesConstRef _payload,
        boostssl::ws::Options options, P2PResponseCallback _callback) override;

    void asyncBroadcastMessageToP2PNodes(
        int16_t _type, bytesConstRef _payload, boostssl::ws::Options _options) override;

    void asyncSendMessageByP2PNodeIDs(int16_t _type, const std::vector<P2pID>& _nodeIDs,
        bytesConstRef _payload, boostssl::ws::Options _options) override;

    void registerHandlerByMsgType(uint32_t _type, MessageHandler _p2pMsgHandler) override
    {
        auto handler = [_p2pMsgHandler](bcos::boostssl::MessageFace::Ptr _message,
                           bcos::boostssl::ws::WsSession::Ptr _session) {
            _p2pMsgHandler(std::dynamic_pointer_cast<P2PMessage>(_message),
                std::dynamic_pointer_cast<P2PSession>(_session));
        };
        if (!m_wsService->registerMsgHandler(_type, handler))
        {
            SERVICE_LOG(INFO) << "registerMsgHandler failed, maybe msgType has a handler"
                              << LOG_KV("msgType", _type);
        }
    }

    MessageHandler getMessageHandlerByMsgType(uint32_t _type)
    {
        auto msgHandler = m_wsService->getMsgHandler(_type);
        return msgHandler;
    }

    void eraseHandlerByMsgType(uint32_t _type) override
    {
        if (!m_wsService->eraseMsgHandler(_type))
        {
            SERVICE_LOG(INFO) << "eraseMsgHandler failed, maybe msgType has a handler"
                              << LOG_KV("msgType", _type);
        }
    }

    bool connected(std::string const& _nodeID) override;

    // todo: for debug, to be removed
    void reportConnectedNodes();

private:
    std::shared_ptr<P2PMessage> newP2PMessage(int16_t _type, bytesConstRef _payload);
    // handshake protocol
    void asyncSendProtocol(bcos::boostssl::ws::WsSession::Ptr _session);
    void onReceiveProtocol(
        boostssl::MessageFace::Ptr _message, std::shared_ptr<P2PSession> _session);

private:
    std::vector<std::function<void(P2PSession::Ptr)>> m_disconnectionHandlers;

    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;

    std::map<boostssl::NodeIPEndpoint, P2pID> m_staticNodes;
    bcos::RecursiveMutex x_nodes;

    std::shared_ptr<boostssl::ws::WsService> m_wsService;

    std::unordered_map<P2pID, P2PSession::Ptr> m_sessions;
    mutable bcos::RecursiveMutex x_sessions;

    std::shared_ptr<boostssl::MessageFaceFactory> m_messageFactory;

    P2pID m_p2pID;

    bool m_run = false;

    // the local protocol
    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;
    bcos::protocol::ProtocolInfoCodec::ConstPtr m_codec;
    P2pInfo m_p2pInfo;

    // todo: add for test, to be removed
    std::shared_ptr<boost::asio::deadline_timer> m_heartbeat;
};

}  // namespace gateway
}  // namespace bcos
