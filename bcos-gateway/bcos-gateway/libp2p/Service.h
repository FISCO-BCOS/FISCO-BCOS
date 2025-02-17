/** @file Service.h
 *  @author monan
 *  @modify first draft
 *  @date 20180910
 *  @author chaychen
 *  @modify realize encode and decode, add timeout, code format
 *  @date 20180911
 */

#pragma once
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/gateway/GatewayTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/ProtocolInfoCodec.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/libp2p/P2PSession.h>
#include <array>


namespace bcos::gateway
{
class Host;
class P2PMessage;
class Gateway;

class Service : public P2PInterface, public std::enable_shared_from_this<Service>
{
public:
    Service(P2PInfo const& _p2pInfo);
    ~Service() override { stop(); }

    using Ptr = std::shared_ptr<Service>;

    void start() override;
    void stop() override;
    virtual void heartBeat();

    virtual bool active();
    P2pID id() const override;

    virtual void onConnect(
        NetworkException e, P2PInfo const& p2pInfo, std::shared_ptr<SessionFace> session);
    virtual void onDisconnect(NetworkException e, P2PSession::Ptr p2pSession);
    virtual void onMessage(NetworkException e, SessionFace::Ptr session, Message::Ptr message,
        std::weak_ptr<P2PSession> p2pSessionWeakPtr);

    virtual std::optional<bcos::Error> onBeforeMessage(SessionFace& _session, Message& _message);

    virtual void registerUnreachableHandler(std::function<void(std::string)> /*unused*/);

    void sendRespMessageBySession(
        bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession) override;

    std::shared_ptr<P2PMessage> sendMessageByNodeID(
        P2pID nodeID, std::shared_ptr<P2PMessage> message) override;

    void asyncSendMessageByNodeID(P2pID nodeID, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback, Options options = Options()) override;

    task::Task<Message::Ptr> sendMessageByNodeID(P2pID nodeID, P2PMessage& header,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options()) override;

    void asyncBroadcastMessage(std::shared_ptr<P2PMessage> message, Options options) override;

    virtual std::map<NodeIPEndpoint, P2pID> staticNodes();
    virtual void setStaticNodes(const std::set<NodeIPEndpoint>& staticNodes);

    P2PInfos sessionInfos() override;  ///< Only connected node
    P2PInfo localP2pInfo() override;
    bool isConnected(P2pID const& nodeID) const override;
    bool isReachable(P2pID const& _nodeID) const override;

    std::shared_ptr<Host> host() override;
    virtual void setHost(std::shared_ptr<Host> host);

    std::shared_ptr<MessageFactory> messageFactory() override;
    virtual void setMessageFactory(std::shared_ptr<MessageFactory> _messageFactory);

    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory();

    void setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory);
    void updateStaticNodes(std::shared_ptr<SocketFace> const& _s, P2pID const& nodeId);

    void registerDisconnectHandler(std::function<void(NetworkException, P2PSession::Ptr)> _handler);

    std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _nodeID) const override;

    void asyncSendMessageByP2PNodeID(uint16_t _type, P2pID _dstNodeID, bytesConstRef _payload,
        Options options = Options(), P2PResponseCallback _callback = nullptr) override;

    void asyncBroadcastMessageToP2PNodes(
        uint16_t _type, uint16_t moduleID, bytesConstRef _payload, Options _options) override;

    void asyncSendMessageByP2PNodeIDs(uint16_t _type, const std::vector<P2pID>& _nodeIDs,
        bytesConstRef _payload, Options _options) override;

    bool registerHandlerByMsgType(uint16_t _type, MessageHandler const& _msgHandler) override;

    MessageHandler getMessageHandlerByMsgType(uint16_t _type);

    void eraseHandlerByMsgType(uint16_t _type) override;

    void asyncSendMessageByEndPoint(NodeIPEndpoint const& _endPoint, P2PMessage::Ptr message,
        CallbackFuncWithSession callback, Options options = Options());

    void setBeforeMessageHandler(
        std::function<std::optional<bcos::Error>(SessionFace&, Message&)> _handler);

    void setOnMessageHandler(
        std::function<std::optional<bcos::Error>(SessionFace::Ptr, Message::Ptr)> _handler);

    void updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable) override;
    void updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable) override;

    virtual std::string getShortP2pID(std::string const& rawP2pID) const;
    virtual std::string getRawP2pID(std::string const& shortP2pID) const;

    virtual void resetP2pID(P2PMessage&, bcos::protocol::ProtocolVersion const&);

protected:
    virtual void sendMessageToSession(P2PSession::Ptr _p2pSession, P2PMessage::Ptr _msg,
        Options = Options(), CallbackFuncWithSession = CallbackFuncWithSession());

    std::shared_ptr<P2PMessage> newP2PMessage(uint16_t _type, bytesConstRef _payload);
    // handshake protocol
    void asyncSendProtocol(P2PSession::Ptr _session);
    void onReceiveProtocol(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);
    void onReceiveHeartbeat(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message);

    // handlers called when new-session
    void registerOnNewSession(std::function<void(P2PSession::Ptr)> _handler);
    // handlers called when delete-session
    void registerOnDeleteSession(std::function<void(P2PSession::Ptr)> _handler);


    virtual void callNewSessionHandlers(P2PSession::Ptr _session);
    virtual void callDeleteSessionHandlers(P2PSession::Ptr _session);

    friend class ServiceV2;

    using SessionsType = std::map<std::string, P2PSession::Ptr>;
    std::vector<std::function<void(NetworkException, P2PSession::Ptr)>> m_disconnectionHandlers;

    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;

    std::map<NodeIPEndpoint, P2pID> m_staticNodes;
    bcos::RecursiveMutex x_nodes;
    std::shared_ptr<Host> m_host;

    // long p2pID to session
    SessionsType m_sessions;
    mutable bcos::SharedMutex x_sessions;

    std::shared_ptr<MessageFactory> m_messageFactory;

    P2PInfo m_selfInfo;
    P2pID m_nodeID;
    std::optional<boost::asio::deadline_timer> m_timer;
    bool m_run = false;

    std::array<MessageHandler, bcos::gateway::GatewayMessageType::All> m_msgHandlers{};

    // the local protocol
    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;
    bcos::protocol::ProtocolInfoCodec::ConstPtr m_codec;

    // handlers called when new-session
    std::vector<std::function<void(P2PSession::Ptr)>> m_newSessionHandlers;
    // handlers called when delete-session
    std::vector<std::function<void(P2PSession::Ptr)>> m_deleteSessionHandlers;

    std::function<std::optional<bcos::Error>(SessionFace&, Message&)> m_beforeMessageHandler;

    std::function<std::optional<bcos::Error>(SessionFace::Ptr, Message::Ptr)> m_onMessageHandler;
    // bcos::LogLevel m_connectionLogLevel = bcos::LogLevel::WARNING;
};

}  // namespace bcos::gateway
