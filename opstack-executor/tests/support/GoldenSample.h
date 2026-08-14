#pragma once
// Golden sample loading + OP header decode + engine_newPayloadV4 params construction.
// The golden encodedHeaderHex is op-geth v1.101702.2's full RLP header; parsed via FISCO's
// BlockHeaderImpl::decodeOpHeader (BlockHeader.h:206, strict 21-field RLP inverse), and the
// accessors build the params. Timestamp units: decodeOpHeader reads OP seconds and stores
// FISCO milliseconds (x1000, BlockHeader.h:195 comment); params must convert back to OP
// seconds (/1000).
#include "SeedPreState.h"
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <string>

namespace w6test
{

inline Json::Value loadJsonFile(std::string const& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ss.str(), root))
        throw std::runtime_error("loadJsonFile: parse failed: " + path);
    return root;
}

struct GoldenSample
{
    std::string id;
    Json::Value vector;  // vectors/<id>.json -> [<id>] (contains env/pre/_op_expected)
    Json::Value golden;  // golden/engine/<id>.golden.json (contains
                         // rawTransactions/encodedHeaderHex/blockHash)
    bool jovian = false;
};

inline bool isJovianVector(Json::Value const& vec)
{
    const auto hardfork = vec["_info"]["hardfork"].asString();
    if (hardfork == "jovian")
        return true;
    if (hardfork != "isthmus")
        throw std::runtime_error("_info.hardfork must be exactly isthmus|jovian, got " + hardfork);
    return false;
}

inline GoldenSample loadVectorSample(std::string const& id)
{
    GoldenSample sample;
    sample.id = id;
    auto root = loadJsonFile(std::string(OP_T8N_VECTORS_DIR) + "/" + id + ".json");
    sample.vector = root[id];
    sample.golden = loadJsonFile(std::string(OP_T8N_GOLDEN_ENGINE_DIR) + "/" + id + ".golden.json");
    sample.jovian = isJovianVector(sample.vector);
    return sample;
}

inline GoldenSample loadChainedSample(std::string const& name)
{
    GoldenSample sample;
    sample.id = name;
    sample.vector =
        loadJsonFile(std::string(OP_T8N_GOLDEN_ENGINE_DIR) + "/chained/" + name + ".golden.json");
    sample.golden = sample.vector;  // flat document is both vector and golden
    sample.jovian = isJovianVector(sample.vector);
    return sample;
}

/// Parses golden.encodedHeaderHex into a FISCO BlockHeaderImpl via decodeOpHeader.
/// Returns nullptr on failure (decodeOpHeader returns Error::UniquePtr; non-null = failed).
inline bcostars::protocol::BlockHeaderImpl::Ptr decodeGoldenHeader(GoldenSample const& sample)
{
    auto bytes = bcos::fromHex(sample.golden["encodedHeaderHex"].asString());
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    // Non-validating OP/ETH header RLP decode (EthBlockHeader::decodeTarsHeader — toTarsHeader's
    // validateHeader rejects NON_ETH headers). Writes the 3 post-merge constants + all fields,
    // RLP seconds → ms timestamp.
    if (auto err = bcos::protocol::EthBlockHeader::decodeTarsHeader(header, bcos::ref(bytes));
        err != nullptr)
        throw std::runtime_error("decodeGoldenHeader: " + err->errorMessage());
    return header;
}

inline std::string quantityOf(bcos::u256 const& v)
{
    // Warning: do not use v.str(16) — Boost multiprecision's str(streamsize, fmtflags)
    // first arg is the number of digits, not the base; fmtflags(0) = decimal.
    // bcos::toQuantity is the correct helper (DataConvertUtility.h:468).
    return bcos::toQuantity(v);
}

inline std::string hexOfBytes(bcos::bytes const& b)
{
    std::string out = "0x";
    for (auto byte : b)
        out += "0123456789abcdef"[byte >> 4], out += "0123456789abcdef"[byte & 0xf];
    return out;
}

inline std::string hexPrefixedH256(bcos::h256 const& h)
{
    return h.hexPrefixed();
}

/// Builds the engine_newPayloadV4 params JSON from the golden sample:
/// [ExecutionPayload, expectedBlobVersionedHashes=[], parentBeaconBlockRoot].
/// Field names strictly follow the engine_newPayloadV4 ExecutionPayload schema
/// (keys read by parseNewPayloadRequest, EngineHelper.cpp:26-136); the shape follows
/// W1 EngineHelperTest.cpp's V4 params.
inline Json::Value makeParamsJson(GoldenSample const& sample)
{
    auto header = decodeGoldenHeader(sample);
    auto const& golden = sample.golden;

    Json::Value ep(Json::objectValue);
    ep["parentHash"] = hexPrefixedH256(header->parentInfo().blockHash);
    ep["feeRecipient"] = "0x" + bcos::toHex(header->coinbase());  // Address is a contiguous range
    ep["stateRoot"] = hexPrefixedH256(header->stateRoot());
    ep["receiptsRoot"] = hexPrefixedH256(header->receiptsRoot());
    ep["logsBloom"] =
        hexOfBytes(bcos::bytes(header->logsBloom().begin(), header->logsBloom().end()));
    ep["prevRandao"] = hexPrefixedH256(header->prevRandao());
    ep["blockNumber"] = quantityOf(bcos::u256(header->number()));
    ep["gasLimit"] = quantityOf(header->gasLimit());
    ep["gasUsed"] = quantityOf(header->gasUsed());
    // timestamp: OP seconds (decodeOpHeader stored milliseconds; /1000)
    ep["timestamp"] = quantityOf(bcos::u256(header->timestamp() / 1000));
    ep["extraData"] =
        hexOfBytes(header->extraData().toBytes());         // extraData() returns bytesConstRef
    ep["baseFeePerGas"] = quantityOf(*header->baseFee());  // baseFee() returns optional<u256>
    ep["blockHash"] = golden["blockHash"].asString();
    // OP path requires present-and-empty (validateOpNewPayloadRequest
    // EngineServiceImpl.cpp:301-303)
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    Json::Value txs(Json::arrayValue);
    for (auto const& raw : golden["rawTransactions"])
        txs.append(raw.asString());
    ep["transactions"] = txs;
    if (header->withdrawalsRoot())
        ep["withdrawalsRoot"] = hexPrefixedH256(*header->withdrawalsRoot());
    ep["blobGasUsed"] =
        quantityOf(*header->blobGasUsed());  // optional<u256>, always filled by decodeOpHeader
    ep["excessBlobGas"] =
        quantityOf(*header->excessBlobGas());  // optional<u256>, always filled by decodeOpHeader

    Json::Value params(Json::arrayValue);
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes = []
    if (header->parentBeaconBlockRoot())
        params.append(hexPrefixedH256(*header->parentBeaconBlockRoot()));
    else
        params.append(Json::Value(Json::nullValue));
    return params;
}

/// Invalid-vector sample. `vector` is the full vector document (contains
/// `_info`/`pre`/`_op_payload`/`_op_expected.reject`, optional `_op_canonical` — the
/// -32603 two-pour canonical-sibling carrier).
struct InvalidSample
{
    Json::Value vector;
    bool jovian = false;
    std::string hardfork;
};

/// Builds the ExecutionPayload JSON verbatim from `_op_payload` (the input to the engine's
/// direct parseNewPayloadRequest). Field names/units align with makeParamsJson (timestamp
/// seconds; Quantity hex); corrupted values + recomputed blockHash + base truths
/// (logsBloom/extraData/withdrawalsRoot etc.) all come from `_op_payload`.
/// - `withdrawals` is a fixed OP-path requirement (present-and-empty,
///   EngineServiceImpl.cpp:332); when the vector omits it, pad an empty array;
/// - null `blobGasUsed`/`excessBlobGas` in `_op_payload` must not enter ep —
///   parseNewPayloadRequest's `isMember` + `fromBigQuantity("")` would throw; remove them;
/// - `parentBeaconBlockRoot` is params[2] (not an ExecutionPayload field), so it does not
///   enter ep.
inline Json::Value makeInvalidParamsJson(InvalidSample const& sample)
{
    auto const& op = sample.vector["_op_payload"];
    Json::Value ep(Json::objectValue);
    for (auto const& member : op.getMemberNames())
    {
        if (member == "parentBeaconBlockRoot")
            continue;  // not an ExecutionPayload field; passed via params[2]
        ep[member] = op[member];
    }
    if (!ep.isMember("withdrawals"))
        ep["withdrawals"] = Json::Value(Json::arrayValue);
    if (!ep.isMember("transactions"))
        ep["transactions"] = Json::Value(Json::arrayValue);  // static-face rawTransactions missing
    if (ep.isMember("blobGasUsed") && ep["blobGasUsed"].isNull())
        ep.removeMember("blobGasUsed");
    if (ep.isMember("excessBlobGas") && ep["excessBlobGas"].isNull())
        ep.removeMember("excessBlobGas");

    Json::Value params(Json::arrayValue);
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes = []
    if (op.isMember("parentBeaconBlockRoot") && !op["parentBeaconBlockRoot"].isNull())
        params.append(op["parentBeaconBlockRoot"]);
    else
        params.append(Json::Value(Json::nullValue));
    return params;
}

/// On-disk corpus loading (the generator emits `invalid_*.json`; the outer `{ "<stem>": {...} }`
/// wrapper matches existing vectors).
inline InvalidSample loadInvalidSample(std::string const& id)
{
    InvalidSample sample;
    auto root = loadJsonFile(std::string(OP_T8N_VECTORS_DIR) + "/" + id + ".json");
    sample.vector = root[id];
    sample.hardfork = sample.vector["_info"]["hardfork"].asString();
    sample.jovian = isJovianVector(sample.vector);
    return sample;
}

}  // namespace w6test
