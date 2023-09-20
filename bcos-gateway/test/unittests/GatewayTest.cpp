#include "bcos-crypto/signature/key/KeyFactoryImpl.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-gateway/GatewayFactory.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::gateway;

struct P2PBaseMock : public P2PInterface
{
    void start() override {}
    void stop() override {}

    P2pID id() const override { return {}; }

    std::shared_ptr<P2PMessage> sendMessageByNodeID(
        P2pID nodeID, std::shared_ptr<P2PMessage> message) override
    {
        return {};
    }
    void asyncSendMessageByNodeID(P2pID nodeID, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback, Options options = Options()) override
    {}
    void asyncBroadcastMessage(std::shared_ptr<P2PMessage> message, Options options) override {}
    P2PInfos sessionInfos() override { return {}; }
    P2PInfo localP2pInfo() override { return {}; }

    bool isConnected(P2pID const& _nodeID) const override { return {}; }
    bool isReachable(P2pID const& _nodeID) const override { return {}; }
    std::shared_ptr<Host> host() override { return {}; }

    std::shared_ptr<MessageFactory> messageFactory() override { return {}; }
    std::shared_ptr<P2PSession> getP2PSessionByNodeId(P2pID const& _nodeID) override { return {}; }
    void asyncSendMessageByP2PNodeID(int16_t _type, P2pID _dstNodeID, bytesConstRef _payload,
        Options options = Options(), P2PResponseCallback _callback = nullptr) override
    {}

    void asyncBroadcastMessageToP2PNodes(
        int16_t _type, uint16_t moduleID, bytesConstRef _payload, Options _options) override
    {}

    void asyncSendMessageByP2PNodeIDs(int16_t _type, const std::vector<P2pID>& _nodeIDs,
        bytesConstRef _payload, Options _options) override
    {}

    void registerHandlerByMsgType(int16_t _type, MessageHandler const& _msgHandler) override {}
    void eraseHandlerByMsgType(int16_t _type) override {}
    void sendRespMessageBySession(bytesConstRef _payload, P2PMessage::Ptr _p2pMessage,
        std::shared_ptr<P2PSession> _p2pSession) override
    {}
};

struct MockGatewayNodeManager : public GatewayNodeManager
{
};

struct GatewayTestFixture
{
};

BOOST_FIXTURE_TEST_SUITE(GatewayTest, GatewayTestFixture)

BOOST_AUTO_TEST_CASE(readonly)
{
    struct ExpectFailed : public P2PBaseMock
    {
        void sendRespMessageBySession(bytesConstRef _payload, P2PMessage::Ptr _p2pMessage,
            std::shared_ptr<P2PSession> _p2pSession) override
        {
            BOOST_FAIL("Unexcepted");
        }
    };
    auto expectFailed = std::make_shared<ExpectFailed>();
    auto gateway = std::make_shared<bcos::gateway::Gateway>(
        "test_chain", expectFailed, nullptr, nullptr, nullptr, "testGateway");
    gateway->enableReadOnlyMode();

    auto factory = std::make_shared<P2PMessageFactoryV2>();
    auto message = std::static_pointer_cast<P2PMessage>(factory->buildMessage());

    // only version >= V2 support p2p network compress
    auto payload = std::make_shared<bytes>(10000, 'a');
    message->setVersion(2);
    message->setSeq(0x12345678);
    message->setPacketType(GatewayMessageType::PeerToPeerMessage);
    message->setExt(0x1101);
    message->setPayload(payload);

    auto options = std::make_shared<P2PMessageOptions>();
    std::string groupID = "group";
    std::string srcNodeID = "nodeID";
    std::string dstNodeID = "nodeID";
    auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
    auto dstNodeIDPtr = std::make_shared<bytes>(dstNodeID.begin(), dstNodeID.end());

    options->setGroupID(groupID);
    options->setSrcNodeID(srcNodeIDPtr);
    auto& dstNodeIDs = options->dstNodeIDs();
    dstNodeIDs.push_back(dstNodeIDPtr);
    dstNodeIDs.push_back(dstNodeIDPtr);
    options->setModuleID(protocol::SYNC_PUSH_TRANSACTION);
    message->setOptions(options);
    gateway->onReceiveP2PMessage(bcos::gateway::NetworkException{}, nullptr, message);
}

BOOST_AUTO_TEST_SUITE_END()