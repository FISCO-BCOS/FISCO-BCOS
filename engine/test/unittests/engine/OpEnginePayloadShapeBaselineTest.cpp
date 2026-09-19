/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file OpEnginePayloadShapeBaselineTest.cpp
 * @brief M2: engine_getPayload envelope shape baseline against the pinned corpus.
 */
// The corpus envelopes are op-geth's engine response bytes. Their SHAPE is NOT the CL's
// shape (op-node's eth.ExecutionPayload): the dump omits withdrawalsRoot that the CL sets
// from Isthmus on (op-service/eth/types.go:426-427/475/483), carries a slotNumber the CL
// type does not have, emits withdrawals: [] even for Regolith where the CL omits it, and
// renders nil blob pointers as explicit null. Both shapes are asserted here as data;
// execution agreement (stateRoot) is M8's job, not this file's.
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/Types.h>  // ApiVersion, ExecutionPayload, NewPayloadRequest
#include <bcos-ledger/mpt/Constants.h>    // emptyRootHash
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using bcos::engine::ApiVersion;
using bcos::engine::OpForkId;

namespace
{
constexpr std::string_view c_getPayloadDir = OP_GETPAYLOAD_DIR;

/// The envelope corpus is generated (absent from a bare checkout, which only carries the
/// symlink's target when the corpus cache is provisioned). Absence skips locally and fails
/// in CI via the FISCO_REQUIRE_T8N_CORPUS macro, mirroring GetPayloadEnvelopeCorpusIsPinned.
/// Returns false when the caller must skip.
bool corpusPresentOrFail()
{
    if (fs::exists(fs::path(c_getPayloadDir)))
    {
        return true;
    }
#ifdef FISCO_REQUIRE_T8N_CORPUS
    BOOST_FAIL("CI requires the getpayload corpus at " << c_getPayloadDir
                                                       << " — regenerating it is Plan C");
#else
    BOOST_TEST_MESSAGE("getpayload corpus absent; run the corpus regen ritual -- skipping");
#endif
    return false;
}

Json::Value loadJsonFile(std::string const& name)
{
    std::ifstream in(std::string(c_getPayloadDir) + "/" + name);
    BOOST_REQUIRE_MESSAGE(in.is_open(), "missing golden " << name << " under " << c_getPayloadDir);
    Json::Value root;
    std::string errors;
    Json::CharReaderBuilder builder;
    BOOST_REQUIRE_MESSAGE(Json::parseFromStream(builder, in, &root, &errors),
        "cannot parse " << name << ": " << errors);
    return root;
}

std::vector<std::string> sortedKeys(Json::Value const& object)
{
    auto keys = object.getMemberNames();
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::string> sortedRange(char const* const* first, char const* const* last)
{
    std::vector<std::string> out(first, last);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> sorted(std::initializer_list<char const*> keys)
{
    return sortedRange(keys.begin(), keys.end());
}

template <std::size_t N>
std::vector<std::string> sorted(char const* const (&keys)[N])
{
    return sortedRange(keys, keys + N);
}

/// Measured 9/9: every dumped executionPayload carries exactly these 18 keys, in this
/// order. A corpus regeneration that changes the dump shape must be reviewed here.
constexpr char const* c_dumpPayloadKeys[] = {"parentHash", "feeRecipient", "stateRoot",
    "receiptsRoot", "logsBloom", "prevRandao", "blockNumber", "gasLimit", "gasUsed", "timestamp",
    "extraData", "baseFeePerGas", "blockHash", "transactions", "withdrawals", "blobGasUsed",
    "excessBlobGas", "slotNumber"};

constexpr char const* c_envelopeV2Keys[] = {
    "executionPayload", "blockValue", "blobsBundle", "executionRequests", "shouldOverrideBuilder"};
constexpr char const* c_envelopeV3PlusKeys[] = {"executionPayload", "blockValue", "blobsBundle",
    "executionRequests", "shouldOverrideBuilder", "parentBeaconBlockRoot"};

/// One (fork, getPayload-version) cell. The four cl* flags are op-node's rules, NOT the
/// dump's; see the file header.
struct Cell
{
    char const* file;
    OpForkId fork;
    ApiVersion getPayloadVersion;
    ApiVersion newPayloadVersion;
    bool clWithdrawals;        // non-nil from Canyon
    bool clWithdrawalsRoot;    // set from Isthmus
    bool clBlobGas;            // non-nil from Ecotone
    bool clParentBeaconRoot;   // envelope member from V3
    bool clExecutionRequests;  // request member from V4
};

constexpr Cell c_cells[] = {
    {"regolith_v2.json", OpForkId::Regolith, ApiVersion::V2, ApiVersion::V2, false, false, false,
        false, false},
    {"canyon_v2.json", OpForkId::Canyon, ApiVersion::V2, ApiVersion::V2, true, false, false, false,
        false},
    {"ecotone_v3.json", OpForkId::Ecotone, ApiVersion::V3, ApiVersion::V3, true, false, true, true,
        false},
    {"fjord_v3.json", OpForkId::Fjord, ApiVersion::V3, ApiVersion::V3, true, false, true, true,
        false},
    {"granite_v3.json", OpForkId::Granite, ApiVersion::V3, ApiVersion::V3, true, false, true, true,
        false},
    {"holocene_v3.json", OpForkId::Holocene, ApiVersion::V3, ApiVersion::V3, true, false, true,
        true, false},
    {"isthmus_v4.json", OpForkId::Isthmus, ApiVersion::V4, ApiVersion::V4, true, true, true, true,
        true},
    {"jovian_v4.json", OpForkId::Jovian, ApiVersion::V4, ApiVersion::V4, true, true, true, true,
        true},
    {"karst_v5.json", OpForkId::Karst, ApiVersion::V5, ApiVersion::V4, true, true, true, true,
        true},
};

/// The CL-shaped executionPayload: the dump with the four documented shape differences
/// normalized away, so this lane's own validator (OpEngineService.cpp:96-146) sees the
/// contract it actually enforces.
Json::Value clShapedPayload(Cell const& cell, Json::Value const& dumped)
{
    Json::Value ep = dumped;  // deep copy
    if (!cell.clWithdrawals)
    {
        // op-geth's encoder emits withdrawals: [] ; the CL omits it before Canyon and this
        // lane rejects a present list pre-Canyon ("withdrawals must be absent").
        ep.removeMember("withdrawals");
    }
    if (cell.clWithdrawalsRoot)
    {
        // op-node sets it from the header at Isthmus+; this lane requires it there. The
        // VALUE is not asserted here (it is the MessagePasser root; decision B / M8 own it).
        ep["withdrawalsRoot"] = "0x" + bcos::ledger::mpt::emptyRootHash().hex();
    }
    if (!cell.clBlobGas)
    {
        // op-node uses omitempty on the blob pointers: nil before Ecotone -> key absent.
        ep.removeMember("blobGasUsed");
        ep.removeMember("excessBlobGas");
    }
    // Not a member of the CL's ExecutionPayload type at all.
    ep.removeMember("slotNumber");
    return ep;
}

/// engine_newPayload params array for the cell's version.
Json::Value clShapedParams(Cell const& cell, Json::Value const& payload, Json::Value const& beacon)
{
    Json::Value params(Json::arrayValue);
    params.append(payload);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes = []
    // ApiVersion is a scoped enum: compare through the underlying type, as this repo's
    // existing engine code does (OpEngineService.cpp casts the same way).
    if (static_cast<std::uint32_t>(cell.newPayloadVersion) >=
        static_cast<std::uint32_t>(ApiVersion::V3))
    {
        params.append(beacon);
    }
    if (static_cast<std::uint32_t>(cell.newPayloadVersion) >=
        static_cast<std::uint32_t>(ApiVersion::V4))
    {
        params.append(Json::Value(Json::arrayValue));  // executionRequests = []
    }
    return params;
}

void requireSamePayload(
    bcos::engine::ExecutionPayload const& a, bcos::engine::ExecutionPayload const& b)
{
    BOOST_CHECK(a.parentHash == b.parentHash);
    BOOST_CHECK(a.stateRoot == b.stateRoot);
    BOOST_CHECK(a.receiptsRoot == b.receiptsRoot);
    BOOST_CHECK(a.blockHash == b.blockHash);
    BOOST_CHECK(a.prevRandao == b.prevRandao);
    BOOST_CHECK(a.feeRecipient == b.feeRecipient);
    BOOST_CHECK(a.gasLimit == b.gasLimit);
    BOOST_CHECK(a.gasUsed == b.gasUsed);
    BOOST_CHECK(a.baseFeePerGas == b.baseFeePerGas);
    BOOST_CHECK_EQUAL(a.blockNumber, b.blockNumber);
    BOOST_CHECK_EQUAL(a.timestamp, b.timestamp);
    BOOST_CHECK(a.extraData == b.extraData);
    BOOST_CHECK(a.logsBloom == b.logsBloom);
    BOOST_CHECK_EQUAL(a.transactions.size(), b.transactions.size());
    // Value-level (not just presence): the serializer writes index/validatorIndex/address/
    // amount per withdrawal and toQuantity()/hexPrefixed() for the optionals; a value drift
    // must fail here the same way a dropped key does.
    BOOST_CHECK(a.withdrawals == b.withdrawals);
    BOOST_CHECK(a.withdrawalsRoot == b.withdrawalsRoot);
    BOOST_CHECK(a.blobGasUsed == b.blobGasUsed);
    BOOST_CHECK(a.excessBlobGas == b.excessBlobGas);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEnginePayloadShapeBaselineSuite)

/// Cell A: the dumped shape is pinned per cell (18-key payload, 5/6-key envelope, and the
/// four dump-only renderings). This is what catches a corpus regen changing the dump.
// clang-format off
BOOST_AUTO_TEST_CASE(DumpedEnvelopeShapeIsPinnedPerCell, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    if (!corpusPresentOrFail())
    {
        return;
    }
    std::size_t cells = 0;
    for (auto const& cell : c_cells)
    {
        BOOST_TEST_INFO_SCOPE(cell.file);
        auto doc = loadJsonFile(cell.file);
        auto const expectedEnvelope = cell.getPayloadVersion == ApiVersion::V2 ?
                                          sorted(c_envelopeV2Keys) :
                                          sorted(c_envelopeV3PlusKeys);
        BOOST_CHECK_MESSAGE(
            sortedKeys(doc) == expectedEnvelope, "envelope key set changed for " << cell.file);
        BOOST_REQUIRE(doc["executionPayload"].isObject());
        Json::Value const& payload = doc["executionPayload"];
        auto const expectedPayload = sorted({c_dumpPayloadKeys[0], c_dumpPayloadKeys[1],
            c_dumpPayloadKeys[2], c_dumpPayloadKeys[3], c_dumpPayloadKeys[4], c_dumpPayloadKeys[5],
            c_dumpPayloadKeys[6], c_dumpPayloadKeys[7], c_dumpPayloadKeys[8], c_dumpPayloadKeys[9],
            c_dumpPayloadKeys[10], c_dumpPayloadKeys[11], c_dumpPayloadKeys[12],
            c_dumpPayloadKeys[13], c_dumpPayloadKeys[14], c_dumpPayloadKeys[15],
            c_dumpPayloadKeys[16], c_dumpPayloadKeys[17]});
        BOOST_CHECK_MESSAGE(sortedKeys(payload) == expectedPayload,
            "executionPayload key set changed for " << cell.file);
        // Dump-only renderings (op-geth's encoder, not the CL's).
        BOOST_CHECK_MESSAGE(!payload.isMember("withdrawalsRoot"),
            cell.file << ": the op-geth dump carries withdrawalsRoot; both the CL type and this "
                         "lane's V4 contract say it appears only from Isthmus, and the pinned "
                         "dump has never carried it");
        BOOST_REQUIRE(payload.isMember("slotNumber"));
        BOOST_CHECK(payload["slotNumber"].isNull());
        if (cell.getPayloadVersion == ApiVersion::V2)
        {
            BOOST_CHECK(payload["blobGasUsed"].isNull());
            BOOST_CHECK(payload["excessBlobGas"].isNull());
        }
        else
        {
            BOOST_CHECK_EQUAL(payload["blobGasUsed"].asString(), "0x0");
            BOOST_CHECK_EQUAL(payload["excessBlobGas"].asString(), "0x0");
        }
        ++cells;
    }
    BOOST_CHECK_EQUAL(cells, 9U);
}

/// Cell B: the CL's per-fork presence rules, asserted on the normalized payload. This is
/// the rule set this lane's validator enforces (OpEngineService.cpp:96-146); the dump's
/// deviations from it are registered in cell A.
// clang-format off
BOOST_AUTO_TEST_CASE(ClPresenceRulesPerFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    if (!corpusPresentOrFail())
    {
        return;
    }
    for (auto const& cell : c_cells)
    {
        BOOST_TEST_INFO_SCOPE(cell.file);
        auto doc = loadJsonFile(cell.file);
        auto payload = clShapedPayload(cell, doc["executionPayload"]);
        BOOST_CHECK_EQUAL(payload.isMember("withdrawals"), cell.clWithdrawals);
        BOOST_CHECK_EQUAL(payload.isMember("withdrawalsRoot"), cell.clWithdrawalsRoot);
        BOOST_CHECK_EQUAL(payload.isMember("blobGasUsed"), cell.clBlobGas);
        BOOST_CHECK_EQUAL(payload.isMember("excessBlobGas"), cell.clBlobGas);
        BOOST_CHECK_EQUAL(doc.isMember("parentBeaconBlockRoot"), cell.clParentBeaconRoot);
    }
}

/// Cell C: the CL-shaped params parse through THIS lane's wire parser, and its own
/// serializer round-trips losslessly. This is the "shape is self-consistent" evidence that
/// the naive "reinject the golden verbatim -> VALID" claim was reaching for; VALID itself
/// would require op-geth's stateRoot to equal this lane's, which is M8.
// clang-format off
BOOST_AUTO_TEST_CASE(ClShapedParamsRoundTripThroughThisLanesWireParser, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    if (!corpusPresentOrFail())
    {
        return;
    }
    std::size_t cells = 0;
    for (auto const& cell : c_cells)
    {
        BOOST_TEST_INFO_SCOPE(cell.file);
        auto doc = loadJsonFile(cell.file);
        auto payload = clShapedPayload(cell, doc["executionPayload"]);
        Json::Value beacon = cell.clParentBeaconRoot ? doc["parentBeaconBlockRoot"] : Json::Value{};
        auto parsed = bcos::rpc::parseNewPayloadRequest(
            clShapedParams(cell, payload, beacon), cell.newPayloadVersion);

        BOOST_CHECK_EQUAL(parsed.executionPayload.withdrawals.has_value(), cell.clWithdrawals);
        BOOST_CHECK_EQUAL(
            parsed.executionPayload.withdrawalsRoot.has_value(), cell.clWithdrawalsRoot);
        BOOST_CHECK_EQUAL(parsed.executionPayload.blobGasUsed.has_value(), cell.clBlobGas);
        BOOST_CHECK_EQUAL(parsed.executionPayload.excessBlobGas.has_value(), cell.clBlobGas);
        BOOST_CHECK_EQUAL(parsed.parentBeaconBlockRoot.has_value(), cell.clParentBeaconRoot);
        BOOST_CHECK_EQUAL(parsed.executionRequests.has_value(), cell.clExecutionRequests);

        auto emitted =
            bcos::rpc::serializeExecutionPayload(parsed.executionPayload, cell.newPayloadVersion);
        auto reparsed = bcos::rpc::parseNewPayloadRequest(
            clShapedParams(cell, emitted, beacon), cell.newPayloadVersion);
        requireSamePayload(parsed.executionPayload, reparsed.executionPayload);
        ++cells;
    }
    BOOST_CHECK_EQUAL(cells, 9U);
}

/// Cell D: the registered V4/V5 byte-identity. karst_v5.json is byte-identical to
/// jovian_v4.json for this input (Osaka does not change a plain-transfer payload; V5's
/// BlobsBundleV2 difference needs blobs, which OP chains do not use). Pinned as data so a
/// future corpus change that breaks it is noticed, not explained away.
// clang-format off
BOOST_AUTO_TEST_CASE(V5CellIsByteIdenticalToV4AndThatIsRegistered, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    if (!corpusPresentOrFail())
    {
        return;
    }
    auto jovian = loadJsonFile("jovian_v4.json");
    auto karst = loadJsonFile("karst_v5.json");
    // Same envelopes, different getPayload version asked of the same pinned op-geth.
    BOOST_CHECK_MESSAGE(karst["executionPayload"] == jovian["executionPayload"],
        "karst_v5 and jovian_v4 executionPayloads diverged; if this is a genuine regeneration, "
        "update getpayload/SHA256SUMS and manifest.txt in the same corpus commit and record why "
        "Osaka now changes a plain-transfer payload");
}
BOOST_AUTO_TEST_SUITE_END()
