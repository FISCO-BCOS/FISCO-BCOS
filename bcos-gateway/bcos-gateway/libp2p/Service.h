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
#include <bcos-boostssl/interfaces/NodeInfo.h>
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
    Service();
    virtual ~Service() { stop(); }

    using Ptr = std::shared_ptr<Service>;

    void start() override;
    void stop() override;
    // virtual void heartBeat();

    virtual bool actived() { return m_run; }
    boostssl::nodeID id() const override { return m_nodeID; }
    void setId(const boostssl::nodeID& _nodeID) { m_nodeID = _nodeID; }

    virtual void onConnect(std::shared_ptr<boostssl::ws::WsSession> session);
    virtual void onDisconnect(NetworkException e, P2PSession::Ptr p2pSession);
    virtual void onMessage(NetworkException e, boostssl::ws::WsSession::Ptr session,
        bcos::boostssl::MessageFace::Ptr message, std::weak_ptr<P2PSession> p2pSessionWeakPtr);

    std::shared_ptr<bcos::boostssl::MessageFace> sendMessageByNodeID(
        boostssl::nodeID nodeID, std::shared_ptr<bcos::boostssl::MessageFace> message) override;
    void sendMessageBySession(
        int _packetType, bytesConstRef _payload, boostssl::ws::WsSession::Ptr _p2pSession) override;
    void sendRespMessageBySession(bytesConstRef _payload,
        bcos::boostssl::MessageFace::Ptr _p2pMessage,
        boostssl::ws::WsSession::Ptr _p2pSession) override;
    void asyncSendMessageByNodeID(P2pID nodeID,
        std::shared_ptr<bcos::boostssl::MessageFace> message, CallbackFuncWithSession callback,
        Options options = Options()) override;

    void asyncBroadcastMessage(
        std::shared_ptr<bcos::boostssl::MessageFace> message, Options options) override;

    virtual std::map<boostssl::NodeIPEndpoint, boostssl::nodeID> staticNodes()
    {
        return m_staticNodes;
    }
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

    void obtainNodeInfo(boostssl::NodeInfo& info, std::string const& node_info);

    boostssl::NodeInfos sessionInfos() override;  ///< Only connected node
    boostssl::NodeInfo localP2pInfo() override
    {
        auto p2pInfo = m_wsService->nodeInfo();
        p2pInfo.nodeID = m_nodeID;
        return p2pInfo;
    }
    bool isConnected(boostssl::nodeID const& nodeID) const override;

    std::shared_ptr<boostssl::ws::WsService> wsService() { return m_wsService; }
    virtual void setWsService(std::shared_ptr<boostssl::ws::WsService> wsService)
    {
        m_wsService = wsService;
    }

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

    void updateEndpointToWservice();

    void updateStaticNodes(std::string const& _endPoint, boostssl::nodeID const& nodeID);

    void registerDisconnectHandler(std::function<void(NetworkException, P2PSession::Ptr)> _handler)
    {
        m_disconnectionHandlers.push_back(_handler);
    }

    std::shared_ptr<P2PSession> getP2PSessionByNodeId(boostssl::nodeID const& _nodeID) override
    {
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(_nodeID);
        if (it != m_sessions.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void asyncSendMessageByP2PNodeID(int16_t _type, boostssl::nodeID _dstNodeID,
        bytesConstRef _payload, boostssl::ws::Options options,
        P2PResponseCallback _callback) override;

    void asyncBroadcastMessageToP2PNodes(
        int16_t _type, bytesConstRef _payload, boostssl::ws::Options _options) override;

    void asyncSendMessageByP2PNodeIDs(int16_t _type, const std::vector<boostssl::nodeID>& _nodeIDs,
        bytesConstRef _payload, boostssl::ws::Options _options) override;

    void registerHandlerByMsgType(uint32_t _type, MessageHandler _msgHandler) override
    {
        // todo: m_wsService->registerMsgHandler 考虑对变量m_msgType2Method加锁
        if (!m_wsService->registerMsgHandler(_type, _msgHandler))
        {
            SERVICE_LOG(INFO) << "registerMsgHandler failed, maybe msgType has a handler"
                              << LOG_KV("msgType", _type);
        }
    }

    MessageHandler getMessageHandlerByMsgType(uint32_t _type)
    {
        return m_wsService->getMsgHandler(_type);
    }

    void eraseHandlerByMsgType(uint32_t _type) override { m_wsService->eraseMsgHandler(_type); }

    bool connected(std::string const& _nodeID) override;

    virtual uint32_t newSeq() override { return ++m_seq; }

private:
    std::shared_ptr<P2PMessage> newP2PMessage(int16_t _type, bytesConstRef _payload);
    // handshake protocol
    void asyncSendProtocol(P2PSession::Ptr _session);
    void onReceiveProtocol(
        NetworkException _e, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);

private:
    std::vector<std::function<void(NetworkException, P2PSession::Ptr)>> m_disconnectionHandlers;

    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;

    std::map<boostssl::NodeIPEndpoint, boostssl::nodeID> m_staticNodes;
    bcos::RecursiveMutex x_nodes;

    std::shared_ptr<boostssl::ws::WsService> m_wsService;

    std::unordered_map<boostssl::nodeID, P2PSession::Ptr> m_sessions;
    mutable bcos::RecursiveMutex x_sessions;

    std::shared_ptr<boostssl::MessageFaceFactory> m_messageFactory;

    boostssl::nodeID m_nodeID;

    std::shared_ptr<boost::asio::deadline_timer> m_timer;

    bool m_run = false;

    std::map<int32_t, MessageHandler> m_msgHandlers;
    mutable SharedMutex x_msgHandlers;

    // the local protocol
    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;
    bcos::protocol::ProtocolInfoCodec::ConstPtr m_codec;
};

}  // namespace gateway
}  // namespace bcos
