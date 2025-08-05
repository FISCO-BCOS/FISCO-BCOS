#define BOOST_TEST_MODULE FilterSystemTest
#include <bcos-rpc/filter/FilterRequest.h>
#include <bcos-rpc/filter/FilterSystem.h>
#include <bcos-utilities/Common.h>
#include <json/json.h>
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
        BOOST_TEST(web3JsonRpc != nullptr);
        filterSystem =
            std::make_shared<Web3FilterSystem>(rpc->groupManager(), "test-group", 600000, 10);
    }
    Json::Value onRPCRequestWrapper(std::string_view request, rpc::Sender _diySender = nullptr)
    {
        Json::Value value;
        Json::Reader reader;
        if (!_diySender)
        {
            std::promise<bcos::bytes> promise;
            web3JsonRpc->onRPCRequest(
                request, [&promise](bcos::bytes resp) { promise.set_value(std::move(resp)); });
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
        BOOST_TEST(!resp.isMember("error"));
        BOOST_TEST(resp.isMember("result"));
        BOOST_TEST(resp.isMember("id"));
        BOOST_TEST(resp.isMember("jsonrpc"));
        BOOST_TEST(resp["jsonrpc"].asString() == "2.0");
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
    BOOST_TEST(value.empty());


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
    BOOST_TEST(value.empty());
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

    auto timeout = 2;
    filterSystem =
        std::make_shared<Web3FilterSystem>(rpc->groupManager(), "test-group", timeout, 10);

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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test