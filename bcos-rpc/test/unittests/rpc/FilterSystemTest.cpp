#include "bcos-rpc/jsonrpc/JsonRpcInterface.h"
#include "unittests/common/RPCFixture.h"
#include <bcos-rpc/filter/FilterSystem.h>

#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::rpc::filter;

namespace bcos::test
{
class FilterTestFixture : public RPCFixture
{
public:
    FilterTestFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_CHECK(web3JsonRpc != nullptr);
        filterSystem = std::make_shared<Web3FilterSystem>(
            *ioServicePool->getIOService(), rpc->groupManager(), "test-group", 600000, 10);
    }
    Json::Value onRPCRequestWrapper(std::string_view request, rpc::Sender _diySender = nullptr)
    {
        Json::Value value;
        Json::Reader reader;
        if (!_diySender)
        {
            std::promise<bcos::bytes> promise;
            web3JsonRpc->onRPCRequest(
                request, [&promise](bcos::bytes resp, boost::beast::http::status) {
                    promise.set_value(std::move(resp));
                });
            auto jsonBytes = promise.get_future().get();
            std::string_view json(
                (char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
            reader.parse(json.begin(), json.end(), value);
        }
        else
        {
            web3JsonRpc->onRPCRequest(request, std::move(_diySender));
        }
        return value;
    }
    static void validRespCheck(Json::Value const& resp)
    {
        BOOST_CHECK(!resp.isMember("error"));
        BOOST_CHECK(resp.isMember("result"));
        BOOST_CHECK(resp.isMember("id"));
        BOOST_CHECK(resp.isMember("jsonrpc"));
        BOOST_CHECK(resp["jsonrpc"].asString() == "2.0");
    };

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
    FilterSystem::Ptr filterSystem;
};

BOOST_FIXTURE_TEST_SUITE(FilterSystemTest, FilterTestFixture)

BOOST_AUTO_TEST_CASE(test_new_block_filter)
{
    // Test inserting a new block filter
    auto filterId = task::syncWait(filterSystem->newBlockFilter());
    BOOST_TEST(!filterId.empty());
    auto value = task::syncWait(filterSystem->getFilterChanges(u256(filterId)));
    BOOST_TEST(value.isArray());
    BOOST_CHECK(value.empty());


    // genesis block
    auto const blockSize = m_ledger->blockNumber() + 1;
    for (int i = 0; i < 10; ++i)
    {
        m_ledger->addNewBlock(nullptr, m_ledger->blockNumber() + 1, 10);
    }
    auto const blocks = m_ledger->ledgerData();
    BOOST_CHECK_EQUAL(blocks.size(), (uint64_t)(10 + blockSize));
    // After creating a new block, the filter should return the new block number
    auto changes = task::syncWait(filterSystem->getFilterChanges(u256(filterId)));
    BOOST_TEST(changes.isArray());
    BOOST_TEST(!changes.empty());
    BOOST_TEST(changes.size() == 10);
    for (int i = 0; i < 10; ++i)
    {
        BOOST_CHECK_EQUAL(
            blocks.at(i + blockSize)->blockHeader()->hash().hexPrefixed(), changes[i].asString());
    }
}

BOOST_AUTO_TEST_CASE(test_new_pending_tx_filter)
{
    // Test inserting a new pending transaction filter
    auto filterId = task::syncWait(filterSystem->newPendingTxFilter());
    BOOST_TEST(!filterId.empty());
    auto value = task::syncWait(filterSystem->getFilterChanges(u256(filterId)));
    BOOST_TEST(value.isArray());
    BOOST_CHECK(value.empty());
    auto const blockSize = m_ledger->blockNumber() + 1;
    auto transactionSize = 10;
    for (int i = 0; i < 10; ++i)
    {
        m_ledger->addNewBlock(nullptr, m_ledger->blockNumber() + 1, transactionSize);
    }
    auto const blocks = m_ledger->ledgerData();
    BOOST_CHECK_EQUAL(blocks.size(), (uint64_t)(10 + blockSize));
    // After creating a new block, the filter should return the new block number
    auto changes = task::syncWait(filterSystem->getFilterChanges(u256(filterId)));
    BOOST_TEST(changes.isArray());
    BOOST_TEST(!changes.empty());
    BOOST_CHECK_EQUAL(changes.size(), transactionSize * 10);
    int j = 0;
    for (int i = 0; i < 10; ++i)
    {
        for (auto transaction_hash : blocks.at(i + blockSize)->transactionHashes())
        {
            BOOST_CHECK_EQUAL(transaction_hash.hexPrefixed(), changes[j++].asString());
        }
    }
}

BOOST_AUTO_TEST_CASE(test_new_log_filter)
{
    // Create filter request
    auto request = std::make_shared<Web3FilterRequest>();
    request->setFromBlock(0);
    request->setToBlock(100);

    // Test creating a new log filter
    auto filterId = task::syncWait(filterSystem->newFilter(request));
    BOOST_TEST(!filterId.empty());
}

BOOST_AUTO_TEST_CASE(test_uninstall_filter)
{
    // First create a filter
    auto request = std::make_shared<Web3FilterRequest>();
    request->setFromBlock(0);
    request->setToBlock(100);

    auto filterId = task::syncWait(filterSystem->newFilter(request));

    // Convert filterId string to u256
    u256 id(filterId);

    // Test uninstalling the filter
    bool result = task::syncWait(filterSystem->uninstallFilter(id));
    BOOST_TEST(result);

    // Try to uninstall non-existent filter
    result = task::syncWait(filterSystem->uninstallFilter(u256(999)));
    BOOST_TEST(!result);
}

BOOST_AUTO_TEST_CASE(test_filter_expiration)
{
    auto request = std::make_shared<Web3FilterRequest>();
    request->setFromBlock(0);
    request->setToBlock(100);

    auto timeout = 200;
    filterSystem = std::make_shared<Web3FilterSystem>(
        *ioServicePool->getIOService(), rpc->groupManager(), "test-group", timeout, 10);

    const auto filterId = task::syncWait(filterSystem->newFilter(request));

    // Wait for filter to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout * 2));

    // Trigger cleanup
    filterSystem->cleanUpExpiredFilters();

    // Try to use expired filter
    u256 id(filterId);
    BOOST_CHECK_THROW(
        task::syncWait(filterSystem->getFilterChanges(id)), bcos::rpc::JsonRpcException);
}

// 1001: a one-shot eth_getLogs must never answer a truncated log set. The per-request budget
// is filter_max_process_block (10 in this fixture); a wider window is an error, not a silent
// prefix of the requested range. Streaming (eth_getFilterChanges) keeps its cursor semantics.
BOOST_AUTO_TEST_CASE(test_getLogsOverRangeLimitFailsClosed)
{
    for (int i = 0; i < 20; ++i)
    {
        m_ledger->addNewBlock(nullptr, m_ledger->blockNumber() + 1, 1);
    }
    auto const latest = filterSystem->getLatestBlockNumber();
    BOOST_REQUIRE_GE(latest, 16);

    auto parseRequest = [&latest](std::string const& json) {
        Json::Reader reader;
        Json::Value value;
        BOOST_REQUIRE(reader.parse(json, value));
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(value, latest, /*safeDepth*/ 1, /*finalizedDepth*/ 2, std::nullopt,
            std::nullopt, false);
        return request;
    };

    // 11 blocks (0x5..0xf) is one over the 10-block budget: refuse rather than return a prefix.
    BOOST_CHECK_THROW(task::syncWait(filterSystem->getLogs(
                          parseRequest(R"({"fromBlock":"0x5","toBlock":"0xf"})"))),
        bcos::rpc::JsonRpcException);

    // Exactly the budget (0x5..0xe) still answers.
    auto logs = task::syncWait(
        filterSystem->getLogs(parseRequest(R"({"fromBlock":"0x5","toBlock":"0xe"})")));
    BOOST_TEST(logs.isArray());
}

// `address` and `topics` are optional in the Ethereum filter spec: an omitted or null field
// matches everything, which is what LogMatcher already does for an empty set.
BOOST_AUTO_TEST_CASE(filterRequestAcceptsMissingAddressAndTopics)
{
    Json::Reader reader;
    auto parse = [&reader](std::string const& s) {
        Json::Value value;
        BOOST_REQUIRE(reader.parse(s, value));
        return value;
    };

    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(parse(R"({"fromBlock":"0x0","toBlock":"0x5"})"), 100, 1, 2, std::nullopt,
            std::nullopt, false);
        BOOST_CHECK(request->addresses().empty());
        BOOST_CHECK(request->topics().empty());
    }
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(parse(R"({"fromBlock":"0x0","toBlock":"0x5",
            "address":null,"topics":null})"),
            100, 1, 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK(request->addresses().empty());
        BOOST_CHECK(request->topics().empty());
    }
    // A topics-only request still filters by topic position 0.
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(parse(R"({"fromBlock":"0x0","toBlock":"0x5","topics":["0xaa"]})"), 100, 1,
            2, std::nullopt, std::nullopt, false);
        BOOST_CHECK(request->addresses().empty());
        BOOST_REQUIRE_EQUAL(request->topics().size(), 1U);
        BOOST_CHECK(request->topics()[0].contains("0xaa"));
    }
}

// Finding F (round-2): FilterRequest::fromJson must resolve "latest"/"safe"/"finalized"
// against the REAL head and the configured depths — never the silent latest=0 / depth=0
// defaults — and both the Web3 and legacy entries thread them explicitly.
BOOST_AUTO_TEST_CASE(filterBlockTagResolvesAgainstHeadAndDepths)
{
    protocol::BlockNumber latest = -1;
    m_ledger->asyncGetBlockNumber(
        [&latest](Error::Ptr, protocol::BlockNumber number) { latest = number; });
    BOOST_REQUIRE_GE(latest, 0);

    Json::Reader reader;
    auto parse = [&reader](std::string const& s) {
        Json::Value value;
        BOOST_REQUIRE(reader.parse(s, value));
        return value;
    };

    // "latest" -> the real head, isLatest stays true (flat path).
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(parse(R"({"fromBlock":"latest","toBlock":"latest",
            "address":"0x1234","topics":[]})"),
            latest, /*safeDepth*/ 1, /*finalizedDepth*/ 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK_EQUAL(request->fromBlock(), latest);
        BOOST_CHECK(request->fromIsLatest());
        BOOST_CHECK_EQUAL(request->toBlock(), latest);
        BOOST_CHECK(request->toIsLatest());
    }

    // "safe" (depth 1) -> latest - 1, "finalized" (depth 2) -> latest - 2 (historical).
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(parse(R"({"fromBlock":"safe","toBlock":"finalized",
            "address":"0x1234","topics":[]})"),
            latest, /*safeDepth*/ 1, /*finalizedDepth*/ 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK_EQUAL(request->fromBlock(), latest - 1);
        BOOST_CHECK(!request->fromIsLatest());
        BOOST_CHECK_EQUAL(request->toBlock(), (std::max)(latest - 2, protocol::BlockNumber{0}));
        BOOST_CHECK(!request->toIsLatest());
    }

    // An explicit hex number resolves to that exact height.
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(parse(R"({"fromBlock":"0x0","toBlock":"0x5",
            "address":"0x1234","topics":[]})"),
            latest, /*safeDepth*/ 1, /*finalizedDepth*/ 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK_EQUAL(request->fromBlock(), 0);
        BOOST_CHECK_EQUAL(request->toBlock(), 5);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
