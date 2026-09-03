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
    // vector has env/pre/_op_expected; golden has rawTransactions/encodedHeaderHex/blockHash
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
    // Byte-equivalence gate: EthBlockHeader::rlpEncode must reproduce the golden encodedHeaderHex
    // (the golden was pinned against the former BlockHeader::encodeOpHeader — same 21-field order
    // + NON_ETH ms→s /1000). decodeTarsHeader is the strict inverse; the roundtrip must match.
    bcos::bytes encoded;
    bcos::protocol::EthBlockHeader(*header).rlpEncode(encoded);
    BOOST_CHECK(encoded == bcos::fromHex(sample.golden["encodedHeaderHex"].asString()));
    // computeHash = keccak256(rlpEncode) == golden.blockHash
    BOOST_CHECK_EQUAL(bcos::protocol::EthBlockHeader::computeHash(*header).hex(),
        std::string(sample.golden["blockHash"].asString()).substr(2));
}

BOOST_AUTO_TEST_CASE(MakeParamsJsonShape)
{
    auto sample = w6test::loadVectorSample("jovian_deposit_only");
    auto params = w6test::makeParamsJson(sample);
    // engine_newPayloadV4 params = [ExecutionPayload, blobHashes, parentBeaconBlockRoot,
    // executionRequests]
    BOOST_REQUIRE(params.isArray());
    BOOST_REQUIRE(params.size() >= 4);
    BOOST_CHECK(params[3u].isArray());  // executionRequests must be an array
    auto const& ep = params[0u];
    BOOST_CHECK(ep.isMember("parentHash"));
    BOOST_CHECK(ep.isMember("stateRoot"));
    BOOST_CHECK(ep.isMember("receiptsRoot"));
    BOOST_CHECK(ep.isMember("logsBloom"));
    BOOST_CHECK(ep.isMember("transactions"));
    BOOST_CHECK(ep.isMember("blockHash"));
    BOOST_CHECK(ep.isMember("withdrawalsRoot"));
    // OP path requires withdrawals present-and-empty (validateOpNewPayloadRequest hard requirement)
    BOOST_CHECK(ep.isMember("withdrawals"));
    BOOST_CHECK(ep["withdrawals"].isArray());
    BOOST_CHECK_EQUAL(ep["withdrawals"].size(), 0);
    BOOST_CHECK(ep["timestamp"].asString().size() >= 3);  // "0x..."
    // rawTransactions go into transactions verbatim (parse-layer decode is fault-tolerant; raw is
    // always retained)
    BOOST_CHECK_EQUAL(ep["transactions"].size(), 1);  // jovian_deposit_only has 1 deposit
}

BOOST_AUTO_TEST_CASE(ManifestCorpusConsistency)
{
    // D4: automatic golden-manifest validation — manifest.txt (non-comment lines) ↔
    // vectors/*.json ↔ golden/engine/*.golden.json must be consistent. Guards against
    // missing vectors (should have been generated), orphan vectors, and manifest drift —
    // beyond regen.sh's manual diff, this is checked at test runtime (catches corpus
    // changes even when regen is not rerun).
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

    // manifest.txt: non-comment, non-blank lines -> basename (suffix .json stripped)
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
    // Static items 3/12 (expectedBlobVersionedHashes/executionRequests) are generated but
    // forced out of the manifest (inexpressible through the GoldenSample loader) — set
    // equality exempts them (Task 6, same as OpT8nReplayTest.cpp's isUnregisteredStatic;
    // entries have .json stripped). "_static_3"=9 chars / "_static_12"=10 chars.
    const auto isUnregisteredStatic = [](std::string const& n) {
        return (n.size() >= 9 && n.rfind("_static_3") == n.size() - 9) ||
               (n.size() >= 10 && n.rfind("_static_12") == n.size() - 10);
    };
    for (auto it = vectors.begin(); it != vectors.end();)
    {
        if (isUnregisteredStatic(*it))
            it = vectors.erase(it);
        else
            ++it;
    }
    auto golden = basenameSet(OP_T8N_GOLDEN_ENGINE_DIR, ".golden.json");

    BOOST_CHECK_MESSAGE(manifest == vectors,
        "manifest.txt vs vectors/ basename sets mismatch (missing/orphan/drifted)");
    // golden/engine is a subset of the engine-gate golden ritual: the line-B
    // (precompile-matrix) golden extension is a recorded deferred obligation (the
    // differential gate does not consume golden/, does not block acceptance), so vectors
    // may exceed golden. The assertion is tightened to golden ⊆ vectors: every golden must
    // have a corresponding vector (no orphan golden), while deferred vectors are tolerated.
    BOOST_CHECK_MESSAGE(std::includes(vectors.begin(), vectors.end(), golden.begin(), golden.end()),
        "golden/engine vs vectors/ sets mismatch (orphan golden / missing vector)");
}

BOOST_AUTO_TEST_SUITE_END()
