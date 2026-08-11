#pragma once
// W6 golden 样本装载 + OP header 解码 + engine_newPayloadV4 params 构造。
// golden encodedHeaderHex 是 op-geth v1.101702.2 完整 RLP header；经 FISCO 的
// BlockHeaderImpl::decodeOpHeader（BlockHeader.h:206，21 字段 RLP 严格逆）解析，
// 读 accessor 构造 params。timestamp 注意单位：decodeOpHeader 读 OP 秒存 FISCO 毫秒
// （×1000，BlockHeader.h:195 注释），params 必须回 OP 秒（÷1000）。
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
    Json::Value vector;  // vectors/<id>.json -> [<id>]（含 env/pre/_op_expected）
    Json::Value golden;  // golden/engine/<id>.golden.json（含
                         // rawTransactions/encodedHeaderHex/blockHash）
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
    sample.golden = sample.vector;  // 扁平文档同时是 vector 与 golden
    sample.jovian = isJovianVector(sample.vector);
    return sample;
}

/// 把 golden.encodedHeaderHex 经 decodeOpHeader 解析成 FISCO BlockHeaderImpl。
/// 失败返回 nullptr（decodeOpHeader 返回 Error::UniquePtr，非空即失败）。
inline bcostars::protocol::BlockHeaderImpl::Ptr decodeGoldenHeader(GoldenSample const& sample)
{
    auto bytes = bcos::fromHex(sample.golden["encodedHeaderHex"].asString());
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    auto c = bcos::engine::detail::opHeaderConst();
    // bcos::bytesRef 的 vector 构造函数形参是 vector 指针，不能隐式转换；用 bcos::ref(bytes)
    // （DataConvertUtility 仓库惯例，见 OpReceiptEncodeTest/BlockImplTest）。
    bcos::bytesRef in = bcos::ref(bytes);
    if (auto err = header->decodeOpHeader(in, c); err != nullptr)
        throw std::runtime_error("decodeGoldenHeader: " + err->errorMessage());
    return header;
}

inline std::string quantityOf(bcos::u256 const& v)
{
    // ⚠️ 不得用 v.str(16)——Boost multiprecision 的 str(streamsize, fmtflags) 首参是数字位数
    // 而非进制，fmtflags(0)=十进制。bcos::toQuantity 是正确 helper（DataConvertUtility.h:468）。
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

/// 从 golden 构造 engine_newPayloadV4 params JSON：
/// [ExecutionPayload, expectedBlobVersionedHashes=[], parentBeaconBlockRoot]。
/// 字段名严格遵循 engine_newPayloadV4 规范 ExecutionPayload schema
/// （parseNewPayloadRequest 读取键，EngineHelper.cpp:26-136）；形态以
/// W1 EngineHelperTest.cpp 的 V4 params 为参照。
inline Json::Value makeParamsJson(GoldenSample const& sample)
{
    auto header = decodeGoldenHeader(sample);
    auto const& golden = sample.golden;

    Json::Value ep(Json::objectValue);
    ep["parentHash"] = hexPrefixedH256(header->parentInfo().blockHash);
    ep["feeRecipient"] = "0x" + bcos::toHex(header->coinbase());  // Address 是 contiguous range
    ep["stateRoot"] = hexPrefixedH256(header->stateRoot());
    ep["receiptsRoot"] = hexPrefixedH256(header->receiptsRoot());
    ep["logsBloom"] =
        hexOfBytes(bcos::bytes(header->logsBloom().begin(), header->logsBloom().end()));
    ep["prevRandao"] = hexPrefixedH256(header->prevRandao());
    ep["blockNumber"] = quantityOf(bcos::u256(header->number()));
    ep["gasLimit"] = quantityOf(header->gasLimit());
    ep["gasUsed"] = quantityOf(header->gasUsed());
    // timestamp：OP 秒（decodeOpHeader 存的是毫秒，÷1000）
    ep["timestamp"] = quantityOf(bcos::u256(header->timestamp() / 1000));
    ep["extraData"] = hexOfBytes(header->extraData().toBytes());  // extraData() 返回 bytesConstRef
    ep["baseFeePerGas"] = quantityOf(*header->baseFee());         // baseFee() 返回 optional<u256>
    ep["blockHash"] = golden["blockHash"].asString();
    // OP 路径必须 present-and-empty（validateOpNewPayloadRequest EngineServiceImpl.cpp:301-303）
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    Json::Value txs(Json::arrayValue);
    for (auto const& raw : golden["rawTransactions"])
        txs.append(raw.asString());
    ep["transactions"] = txs;
    if (header->withdrawalsRoot())
        ep["withdrawalsRoot"] = hexPrefixedH256(*header->withdrawalsRoot());
    ep["blobGasUsed"] = quantityOf(*header->blobGasUsed());  // optional<u256>，decodeOpHeader 恒填
    ep["excessBlobGas"] =
        quantityOf(*header->excessBlobGas());  // optional<u256>，decodeOpHeader 恒填

    Json::Value params(Json::arrayValue);
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes = []
    if (header->parentBeaconBlockRoot())
        params.append(hexPrefixedH256(*header->parentBeaconBlockRoot()));
    else
        params.append(Json::Value(Json::nullValue));
    return params;
}

}  // namespace w6test
