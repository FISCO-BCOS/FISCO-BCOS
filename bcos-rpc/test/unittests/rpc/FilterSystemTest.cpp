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

// eth_getFilterLogs is the third caller of getLogsImpl (after eth_getLogs and the legacy
// getLogs), so it inherits the same fail-closed bound. It is easy to miss because
// eth_getFilterChanges -- the other half of the filter workflow -- keeps paginating, so a
// reviewer reading only about eth_getLogs would think the backfill still works.
BOOST_AUTO_TEST_CASE(getFilterLogsOverRangeLimitFailsClosed)
{
    for (int i = 0; i < 20; ++i)
    {
        m_ledger->addNewBlock(nullptr, m_ledger->blockNumber() + 1, 1);
    }
    auto const latest = filterSystem->getLatestBlockNumber();
    BOOST_REQUIRE_GE(latest, 16);

    // Go through fromJson: it is what resolves the block tags and sets the isLatest flags that
    // getLogsImpl reads. A hand-built request left at the defaults would resolve both ends to
    // "latest" and silently test a one-block window.
    auto const newFilterFor = [&latest, this](std::string const& json) {
        Json::Reader reader;
        Json::Value value;
        BOOST_REQUIRE(reader.parse(json, value));
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(value, latest, /*safeDepth*/ 1, /*finalizedDepth*/ 2, std::nullopt,
            std::nullopt, false);
        return task::syncWait(filterSystem->newFilter(request));
    };

    // A filter spanning more than the 10-block budget: its backfill is refused, not truncated.
    auto const wideId = newFilterFor(R"({"fromBlock":"0x0","toBlock":"latest"})");
    BOOST_REQUIRE(!wideId.empty());
    BOOST_CHECK_THROW(
        task::syncWait(filterSystem->getFilterLogs(u256(wideId))), bcos::rpc::JsonRpcException);

    // A filter inside the budget still answers -- the bound, not filters in general, is what
    // this test pins.
    auto const narrowId = newFilterFor(R"({"fromBlock":"latest","toBlock":"latest"})");
    BOOST_REQUIRE(!narrowId.empty());
    auto const logs = task::syncWait(filterSystem->getFilterLogs(u256(narrowId)));
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

// The whole premise of making address/topics optional is that LogMatcher treats an empty
// address set and an absent topic position as "match all". The two tests above cannot falsify
// that claim -- they stop at the parser and at isArray(), both of which hold for an empty
// result. This seeds a block whose receipt carries a real log (status None, unlike
// addNewBlock's fake receipts) and asserts the log actually comes back for a range-only filter
// and for an address-only filter, i.e. the two request shapes the change exists to accept.
BOOST_AUTO_TEST_CASE(getLogsEmptyFilterMatchesAllLogs)
{
    // Append above the fixture's existing chain: overwriting a seeded block would test nothing
    // (and FakeLedger indexes blocks by position).
    auto const number = m_ledger->blockNumber() + 1;
    std::string const logAddress = "abababababababababababababababababababab";
    h256 const topic0("0x1111111111111111111111111111111111111111111111111111111111111111");

    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setParentInfo({number - 1, h256(0U)});
    header->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
    block->setBlockHeader(header);

    // LogMatcher walks the block's transaction metadata (one receipt per entry), so a receipt
    // alone is invisible to eth_getLogs -- the block needs the matching transaction row too.
    // The row only supplies the hash eth_getLogs echoes back, so a synthetic one is enough.
    block->appendTransactionMetaData(
        m_blockFactory->createTransactionMetaData(h256(1U), std::string("/apps/ab")));

    std::vector<protocol::LogEntry> logs;
    logs.emplace_back(toAddress(logAddress).asBytes(), h256s{topic0}, bytes{});
    bytes output;
    auto receipt = m_blockFactory->receiptFactory()->createReceipt(
        u256(21000), logAddress, logs, /*status=*/0, ref(output), number);
    block->appendReceipt(receipt);
    m_ledger->appendBuiltBlock(std::move(block));

    auto const latest = filterSystem->getLatestBlockNumber();
    BOOST_REQUIRE_GE(latest, number);
    auto parseRequest = [&latest](std::string const& json) {
        Json::Reader reader;
        Json::Value value;
        BOOST_REQUIRE(reader.parse(json, value));
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(value, latest, /*safeDepth*/ 1, /*finalizedDepth*/ 2, std::nullopt,
            std::nullopt, false);
        return request;
    };
    auto const blockTag = toQuantity(number);

    // Range only -- neither address nor topics. Before the fix this was rejected outright.
    auto const rangeOnly = task::syncWait(filterSystem->getLogs(
        parseRequest(R"({"fromBlock":")" + blockTag + R"(","toBlock":")" + blockTag + R"("})")));
    BOOST_REQUIRE_EQUAL(rangeOnly.size(), 1U);
    BOOST_CHECK_EQUAL(rangeOnly[0U]["topics"][0U].asString(), topic0.hexPrefixed());
    BOOST_CHECK_EQUAL(rangeOnly[0U]["data"].asString(), "0x");

    // Address only -- no range at all, the shape ethers' queryFilter(event) / web3.js
    // getPastLogs({address}) send. Both range fields default to "latest", and the block just
    // appended is the head, so this must find the log.
    auto const addressOnly = task::syncWait(filterSystem->getLogs(
        parseRequest(R"({"address":"0xabababababababababababababababababababab"})")));
    BOOST_REQUIRE_EQUAL(addressOnly.size(), 1U);

    // A different address still matches nothing: this pins that address filtering is applied,
    // not that every filter returns everything.
    auto const otherAddress = task::syncWait(filterSystem->getLogs(
        parseRequest(R"({"address":"0xcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"})")));
    BOOST_CHECK(otherAddress.empty());
}

// The remaining criteria are optional too, and a blockHash is an alternative to the range: a
// spec-conformant client sends either {fromBlock, toBlock} or {blockHash}, and ethers/web3.js
// omit the range entirely when the caller gives only an address. Each of those shapes used to be
// rejected (or, for blockHash, silently ignored because only the all-lowercase spelling was read).
BOOST_AUTO_TEST_CASE(filterRequestAcceptsBlockHashAndDefaultsRange)
{
    Json::Reader reader;
    auto parse = [&reader](std::string const& s) {
        Json::Value value;
        BOOST_REQUIRE(reader.parse(s, value));
        return value;
    };

    // blockHash only (the JSON-RPC spelling) -- no fromBlock/toBlock at all.
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(
            parse(R"({"blockHash":"0x1234"})"), 100, 1, 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK_EQUAL(request->blockHash(), "0x1234");
    }
    // blockhash, the spelling older builds of this file read and therefore what clients written
    // against them send.
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(
            parse(R"({"blockhash":"0x5678"})"), 100, 1, 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK_EQUAL(request->blockHash(), "0x5678");
    }
    // An omitted range defaults to "latest" (the real head), not block 0 and not an error.
    {
        auto request = std::make_shared<Web3FilterRequest>();
        request->fromJson(
            parse(R"({"address":"0x1234"})"), 100, 1, 2, std::nullopt, std::nullopt, false);
        BOOST_CHECK_EQUAL(request->fromBlock(), 100);
        BOOST_CHECK(request->fromIsLatest());
        BOOST_CHECK_EQUAL(request->toBlock(), 100);
        BOOST_CHECK(request->toIsLatest());
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
