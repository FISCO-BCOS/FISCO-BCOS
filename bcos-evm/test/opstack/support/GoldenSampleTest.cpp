// bcos-evm/test/opstack/support/GoldenSampleTest.cpp
#include "GoldenSample.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <string>

BOOST_AUTO_TEST_SUITE(GoldenSampleSuite)

BOOST_AUTO_TEST_CASE(LoadVectorAndGolden)
{
    auto sample = w6test::loadVectorSample("jovian_deposit_only");
    BOOST_CHECK_EQUAL(sample.id, "jovian_deposit_only");
    // vector 有 env/pre/_op_expected；golden 有 rawTransactions/encodedHeaderHex/blockHash
    BOOST_CHECK(sample.vector.isMember("pre"));
    BOOST_CHECK(sample.vector.isMember("env"));
    BOOST_CHECK(sample.golden.isMember("rawTransactions"));
    BOOST_CHECK(sample.golden.isMember("encodedHeaderHex"));
    BOOST_CHECK(sample.golden.isMember("blockHash"));
    BOOST_CHECK(sample.jovian);  // _info.hardfork == "jovian"
}

BOOST_AUTO_TEST_CASE(DecodeGoldenHeaderRoundTrip)
{
    auto sample = w6test::loadVectorSample("jovian_deposit_only");
    auto header = w6test::decodeGoldenHeader(sample);
    BOOST_REQUIRE(header != nullptr);
    // decodeOpHeader 是 encodeOpHeader 的严格逆；roundtrip 应逐字节一致
    auto c = bcos::engine::detail::opHeaderConst();
    BOOST_CHECK(header->encodeOpHeader(c) ==
        bcos::fromHex(sample.golden["encodedHeaderHex"].asString()));
    // opHeaderHash = keccak256(encodeOpHeader()) == golden.blockHash
    BOOST_CHECK_EQUAL(header->opHeaderHash(c).hex(), std::string(sample.golden["blockHash"].asString()).substr(2));
}

BOOST_AUTO_TEST_CASE(MakeParamsJsonShape)
{
    auto sample = w6test::loadVectorSample("jovian_deposit_only");
    auto params = w6test::makeParamsJson(sample);
    // engine_newPayloadV4 params = [ExecutionPayload, blobHashes, parentBeaconBlockRoot]
    BOOST_REQUIRE(params.isArray());
    BOOST_REQUIRE(params.size() >= 3);
    auto const& ep = params[0u];
    BOOST_CHECK(ep.isMember("parentHash"));
    BOOST_CHECK(ep.isMember("stateRoot"));
    BOOST_CHECK(ep.isMember("receiptsRoot"));
    BOOST_CHECK(ep.isMember("logsBloom"));
    BOOST_CHECK(ep.isMember("transactions"));
    BOOST_CHECK(ep.isMember("blockHash"));
    BOOST_CHECK(ep.isMember("withdrawalsRoot"));
    // OP 路径 withdrawals 必须 present-and-empty（validateOpNewPayloadRequest 硬要求）
    BOOST_CHECK(ep.isMember("withdrawals"));
    BOOST_CHECK(ep["withdrawals"].isArray());
    BOOST_CHECK_EQUAL(ep["withdrawals"].size(), 0);
    BOOST_CHECK(ep["timestamp"].asString().size() >= 3);  // "0x..."
    // rawTransactions 原样进 transactions（parse 层 decode 容错跳过，raw 无条件保留）
    BOOST_CHECK_EQUAL(ep["transactions"].size(), 1);  // jovian_deposit_only 1 笔 deposit
}

BOOST_AUTO_TEST_SUITE_END()
