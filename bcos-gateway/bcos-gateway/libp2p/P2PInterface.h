/** @file P2PInterface.h
 *  @author chaychen
 *  @date 20180911
 */

#pragma once

#include "bcos-task/Task.h"
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/SessionFace.h>
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PMessage.h>

namespace bcos
{
namespace stat
{
class NetworkStatHandler;
class ChannelNetworkStatHandler;
}  // namespace stat

namespace gateway
{
class P2PMessage;
class MessageFactory;
class P2PSession;
using CallbackFuncWithSession =
    std::function<void(NetworkException, std::shared_ptr<P2PSession>, std::shared_ptr<P2PMessage>)>;
using DisconnectCallbackFuncWithSession =
    std::function<void(NetworkException, std::shared_ptr<P2PSession>)>;
using P2PResponseCallback = std::function<void(Error::Ptr, uint16_t, bytesConstRef)>;
class P2PInterface
{
public:
    using Ptr = std::shared_ptr<P2PInterface>;
    virtual ~P2PInterface() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual P2pID id() const = 0;

    virtual std::shared_ptr<P2PMessage> sendMessageByNodeID(
        P2pID nodeID, std::shared_ptr<P2PMessage> message) = 0;

    virtual void asyncSendMessageByNodeID(P2pID nodeID, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback, Options options = Options()) = 0;

    virtual task::Task<Message::Ptr> sendMessageByNodeID(P2pID nodeID, P2PMessage& header,
        ::ranges::any_view<bytesConstRef> payloads, Options options = Options()) = 0;

    virtual void asyncBroadcastMessage(std::shared_ptr<P2PMessage> message, Options options) = 0;

    virtual P2PInfos sessionInfos() = 0;
    virtual P2PInfo localP2pInfo() = 0;

    virtual bool isConnected(P2pID const& _nodeID) const = 0;
    virtual bool isReachable(P2pID const& _nodeID) const = 0;
    virtual std::shared_ptr<Host> host() = 0;

    virtual std::shared_ptr<MessageFactory> messageFactory() = 0;

    virtual std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _nodeID) = 0;

    /**
     * @brief send message to the given p2p nodes
     *
     * @param _type the message type
     * @param _dstNodeID the dst node
     * @param _payload the data
     * @param options timeout option
     * @param _callback called when receive response
     */
    virtual void asyncSendMessageByP2PNodeID(uint16_t _type, P2pID _dstNodeID,
        bytesConstRef _payload, Options options = Options(),
        P2PResponseCallback _callback = nullptr) = 0;

    /**
     * @brief broadcast message to all p2p nodes
     *
     * @param _type the message type
     * @param _payload the payload
     */
    virtual void asyncBroadcastMessageToP2PNodes(
        uint16_t _type, uint16_t moduleID, bytesConstRef _payload, Options _options) = 0;

    /**
     * @brief send message to the given nodeIDs
     */
    virtual void asyncSendMessageByP2PNodeIDs(uint16_t _type, const std::vector<P2pID>& _nodeIDs,
        bytesConstRef _payload, Options _options) = 0;

    using MessageHandler =
        std::function<void(NetworkException, std::shared_ptr<P2PSession>, P2PMessage::Ptr)>;

    virtual bool registerHandlerByMsgType(uint16_t _type, MessageHandler const& _msgHandler) = 0;

    virtual void eraseHandlerByMsgType(uint16_t _type) = 0;

    virtual void sendRespMessageBySession(bytesConstRef _payload, P2PMessage::Ptr _p2pMessage,
        std::shared_ptr<P2PSession> _p2pSession) = 0;

    virtual void updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable) = 0;
    virtual void updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable) = 0;
};

}  // namespace gateway

}  // namespace bcos
