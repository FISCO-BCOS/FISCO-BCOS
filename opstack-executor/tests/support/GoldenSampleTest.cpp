// bcos-evm/test/opstack/support/GoldenSampleTest.cpp
#include "GoldenSample.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

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
    BOOST_CHECK(
        header->encodeOpHeader(c) == bcos::fromHex(sample.golden["encodedHeaderHex"].asString()));
    // opHeaderHash = keccak256(encodeOpHeader()) == golden.blockHash
    BOOST_CHECK_EQUAL(header->opHeaderHash(c).hex(),
        std::string(sample.golden["blockHash"].asString()).substr(2));
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

BOOST_AUTO_TEST_CASE(ManifestCorpusConsistency)
{
    // D4: golden manifest 自动校验——manifest.txt（非注释行）↔ vectors/*.json ↔
    // golden/engine/*.golden.json
    // 三集合必须一致。防漏格（向量该生成未生成）/孤儿向量/清单漂移——regen.sh 的手动 diff 之外,
    // 测试运行时自动检查（语料改动不跑 regen 时也能暴露）。
    auto basenameSet = [](std::filesystem::path const& dir, std::string_view suffix) {
        std::set<std::string> names;
        for (auto const& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            auto name = entry.path().filename().string();
            if (name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                names.insert(name.substr(0, name.size() - suffix.size()));
        }
        return names;
    };

    // manifest.txt：非注释、非空行 → basename（去 .json 后缀）
    std::set<std::string> manifest;
    {
        std::ifstream in(std::string(OP_T8N_VECTORS_DIR) + "/manifest.txt");
        BOOST_REQUIRE(in.good());
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            constexpr std::string_view kJsonSuffix = ".json";
            if (line.size() > kJsonSuffix.size() && line.compare(line.size() - kJsonSuffix.size(),
                                                        kJsonSuffix.size(), kJsonSuffix) == 0)
                line.resize(line.size() - kJsonSuffix.size());
            manifest.insert(line);
        }
    }

    auto vectors = basenameSet(OP_T8N_VECTORS_DIR, ".json");
    auto golden = basenameSet(OP_T8N_GOLDEN_ENGINE_DIR, ".golden.json");

    BOOST_CHECK_MESSAGE(
        manifest == vectors, "manifest.txt ↔ vectors/ basename 集合不一致（漏格/孤儿/漂移）");
    // golden/engine 是 engine-gate golden ritual 的子集：线 B（预编译矩阵）的 golden
    // 扩展是记录在案的延后义务（差分门不消费 golden/，不阻塞验收），故 vectors 可多于
    // golden。断言收紧为 golden ⊆ vectors：每个 golden 必须有对应 vector（无孤儿 golden），
    // 容忍 golden 延后向量（非反向遗漏）。
    BOOST_CHECK_MESSAGE(std::includes(vectors.begin(), vectors.end(), golden.begin(), golden.end()),
        "golden/engine ↔ vectors/ 集合不一致（孤儿 golden / 缺对应 vector）");
}

BOOST_AUTO_TEST_SUITE_END()
