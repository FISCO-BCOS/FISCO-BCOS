/** @file P2PInterface.h
 *  @author chaychen
 *  @date 20180911
 */

#pragma once

#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/interfaces/NodeInfo.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <memory>

namespace bcos
{
namespace stat
{
class NetworkStatHandler;
class ChannelNetworkStatHandler;
}  // namespace stat

namespace gateway
{
class MessageFactory;
class P2PSession;
using CallbackFuncWithSession =
    std::function<void(NetworkException, std::shared_ptr<P2PSession>, std::shared_ptr<bcos::boostssl::MessageFace>)>;
using DisconnectCallbackFuncWithSession =
    std::function<void(NetworkException, std::shared_ptr<P2PSession>)>;
using P2PResponseCallback =
    std::function<void(Error::Ptr&& _error, int16_t, std::shared_ptr<bytes> _data)>;
class P2PInterface
{
public:
    using Ptr = std::shared_ptr<P2PInterface>;
    virtual ~P2PInterface(){};

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual P2pID id() const = 0;
    virtual uint32_t newSeq() = 0;

    virtual std::shared_ptr<bcos::boostssl::MessageFace> sendMessageByNodeID(
        P2pID nodeID, std::shared_ptr<bcos::boostssl::MessageFace> message) = 0;

    virtual void asyncSendMessageByNodeID(P2pID nodeID, std::shared_ptr<bcos::boostssl::MessageFace> message,
        CallbackFuncWithSession callback, Options options = Options()) = 0;

    virtual void asyncBroadcastMessage(std::shared_ptr<bcos::boostssl::MessageFace> message, Options options) = 0;

    virtual boostssl::NodeInfos sessionInfos() = 0;
    virtual boostssl::NodeInfo localP2pInfo() = 0;

    virtual bool isConnected(P2pID const& _nodeID) const = 0;

    virtual std::shared_ptr<bcos::boostssl::MessageFaceFactory> messageFactory() = 0;

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
    virtual void asyncSendMessageByP2PNodeID(int16_t _type, P2pID _dstNodeID,
        bytesConstRef _payload, Options options, P2PResponseCallback _callback) = 0;

    /**
     * @brief broadcast message to all p2p nodes
     *
     * @param _type the message type
     * @param _payload the payload
     */
    virtual void asyncBroadcastMessageToP2PNodes(
        int16_t _type, bytesConstRef _payload, Options _options) = 0;

    /**
     * @brief send message to the given nodeIDs
     */
    virtual void asyncSendMessageByP2PNodeIDs(int16_t _type, const std::vector<P2pID>& _nodeIDs,
        bytesConstRef _payload, Options _options) = 0;

    using MessageHandler =
        std::function<void(std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<boostssl::ws::WsSession>)>;
    virtual void registerHandlerByMsgType(uint32_t _type, MessageHandler const& _msgHandler) = 0;
    virtual void eraseHandlerByMsgType(uint32_t _type) = 0;

    virtual bool connected(std::string const& _nodeID) = 0;
    virtual void sendMessageBySession(
        int _packetType, bytesConstRef _payload, std::shared_ptr<boostssl::ws::WsSession> _p2pSession) = 0;
    virtual void sendRespMessageBySession(bytesConstRef _payload, bcos::boostssl::MessageFace::Ptr _p2pMessage,
        std::shared_ptr<boostssl::ws::WsSession> _p2pSession) = 0;
};

}  // namespace gateway

}  // namespace bcos
