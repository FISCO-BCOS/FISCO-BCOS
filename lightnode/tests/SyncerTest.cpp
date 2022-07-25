#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-lightnode/syncer/BlockSyncerClientImpl.h>
#include <bcos-lightnode/syncer/BlockSyncerServerImpl.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <stdexcept>
#include <type_traits>

using namespace bcos::sync;

using Hash = std::array<std::byte, 32>;

class MockFront : public bcos::front::FrontServiceInterface
{
    void start() override {}
    void stop() override {}

    void asyncGetGroupNodeInfo(bcos::front::GetGroupNodeInfoFunc _onGetGroupNodeInfo) override
    {
        auto nodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>();
        nodeInfo->setNodeIDList({"id1", "id2"});

        _onGetGroupNodeInfo(nullptr, std::move(nodeInfo));
    }

    void onReceiveGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
        bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unspported method"});
    }

    void onReceiveMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unspported method"});
    }

    void onReceiveBroadcastMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unspported method"});
    }

    void asyncSendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, uint32_t _timeout, bcos::front::CallbackFunc _callback) override
    {
        BOOST_CHECK_EQUAL(_moduleID, 0);
        auto& byteData = _nodeID->data();
        std::string_view view((const char*)byteData.data(), byteData.size());

        BOOST_CHECK(view == "id1" || view == "id2");
        BOOST_CHECK_EQUAL(_timeout, 0);

        bcostars::RequestBlock request;
        bcos::concepts::serialize::decode(_data, request);
        BOOST_CHECK_EQUAL(request.beginBlockNumber, 100);
        BOOST_CHECK_EQUAL(request.endBlockNumber, 110);
        BOOST_CHECK_EQUAL(request.onlyHeader, true);

        bcostars::ResponseBlock response;
        response.blockNumber = 500;

        bcostars::Block block;
        block.blockHeader.data.blockNumber = 201;
        block.blockHeader.data.gasUsed = "1000";
        block.transactions.resize(100);
        block.transactions[10].importTime = 500;
        response.blocks.emplace_back(std::move(block));

        std::vector<bcos::byte> outBuffer;
        bcos::concepts::serialize::encode(response, outBuffer);

        _callback(nullptr, std::move(_nodeID), bcos::ref(outBuffer), "", {});
    }

    void asyncSendResponse(const std::string& _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unspported method"});
    }

    void asyncSendMessageByNodeIDs(int _moduleID,
        const std::vector<bcos::crypto::NodeIDPtr>& _nodeIDs, bcos::bytesConstRef _data) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unspported method"});
    }

    void asyncSendBroadcastMessage(
        uint16_t _type, int _moduleID, bcos::bytesConstRef _data) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unspported method"});
    }
};

struct SyncerFixture
{
    SyncerFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(SyncerTest, SyncerFixture)

BOOST_AUTO_TEST_CASE(testClient)
{
    struct MockLedger : public bcos::concepts::ledger::LedgerBase<MockLedger>
    {
        void impl_setBlock(bcostars::Block block)
        {
            auto blockNumber = block.blockHeader.data.blockNumber;
            m_blocks.emplace(std::make_pair(blockNumber, std::move(block)));
        }

        bcos::concepts::ledger::TransactionCount impl_getTotalTransactionCount()
        {
            bcos::concepts::ledger::TransactionCount status;
            status.blockNumber = 100;
            status.total = 10000;
            status.failed = 500;

            return status;
        }

        std::map<int64_t, bcostars::Block>& m_blocks;
    };

    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

    std::map<int64_t, bcostars::Block> blocks;

    MockLedger clientLedger{.m_blocks = blocks};

    auto mockFront = std::make_shared<MockFront>();
    bcos::sync::BlockSyncerClientImpl<MockLedger, bcostars::RequestBlock, bcostars::ResponseBlock>
        syncerClient(std::move(clientLedger), mockFront, keyFactory);

    syncerClient.fetchAndStoreNewBlocks();

    // Check the block is store in the ledger
    BOOST_CHECK_EQUAL(clientLedger.m_blocks.size(), 1);

    auto it = clientLedger.m_blocks.find(201);
    BOOST_CHECK(it != clientLedger.m_blocks.end());

    auto& block = it->second;
    BOOST_CHECK_EQUAL(block.blockHeader.data.blockNumber, 201);
    BOOST_CHECK_EQUAL(block.blockHeader.data.gasUsed, "1000");
}

struct MockLedgerForTestServer : public bcos::concepts::ledger::LedgerBase<MockLedgerForTestServer>
{
    bcos::concepts::ledger::TransactionCount impl_getTotalTransactionCount()
    {
        bcos::concepts::ledger::TransactionCount status;
        status.blockNumber = 204;
        status.total = 10000;
        status.failed = 500;

        return status;
    }

    template <bcos::concepts::ledger::DataFlag... flags>
    void impl_getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        BOOST_CHECK(blockNumber >= 200 && blockNumber < 220);
        if (blockNumber < 205)
        {
            block.blockHeader.data.blockNumber = blockNumber;
        }
        else
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"Block out of range!"});
        }
    }
};

BOOST_AUTO_TEST_CASE(testServer)
{
    MockLedgerForTestServer serverLedger;

    bcos::sync::BlockSyncerServerImpl<MockLedgerForTestServer> syncerServer(
        std::move(serverLedger));

    bcostars::RequestBlock request;
    request.beginBlockNumber = 200;
    request.endBlockNumber = 220;
    request.onlyHeader = true;

    bcostars::ResponseBlock response;
    syncerServer.getBlock(request, response);
    BOOST_CHECK_EQUAL(response.blockNumber, 204);
    BOOST_CHECK_EQUAL(response.blocks.size(), 5);

    for (auto i = 200u; i < 205; ++i)
    {
        BOOST_CHECK_EQUAL(response.blocks[i - 200].blockHeader.data.blockNumber, i);
    }

    request.endBlockNumber = 200;
    BOOST_CHECK_THROW(syncerServer.getBlock(request, response), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()


// static_assert(bcos::concepts::ledger::Ledger<MockLedger>, "");