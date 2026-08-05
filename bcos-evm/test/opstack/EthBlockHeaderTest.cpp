// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EthBlockHeaderTest.cpp — 闭环 Task 3(design §5.1/§7.5,决策 C3/C6)。
//
// 覆盖一个 GTest 套件(brief 裁定 C6,套件名钉死):
//   - EthBlockHeader:      bcos::codec::rlp::encodeOpHeader()/opHeaderHash()/decodeOpHeader()
//     (2026-08-05 迁移:载体从退役的 EthBlockHeader 结构换成 FISCO BlockHeader + OpHeaderConst,
//     RLP 字节与 blockHash 逐字节不变,见 spec 2026-08-05-opstack-blockheader-fisco-adaptation)
//
// 原 OpDepositEncode 套件(OpDepositFields/encodeDepositEnvelope 自由函数对 op-geth golden
// rawTransactions 的逐字节比对)已于 2026-08-05 迁移:编码随退役的 bcos-codec
// OpDepositEncode.h/.cpp 一起内联进 bcos-rpc 的 DepositTxHandler(TxHandler.cpp),golden
// 覆盖随之搬到 bcos-rpc/test/unittests/rpc/OpDepositEncodeTest.cpp。
//
// 金值全部来自 Task 2 的产物,不自算(brief 明示):
//   - t8n/vectors/<id>.json 的 env(+parentHash/parentBeaconBlockRoot/currentCoinbase/
//     currentNumber/currentTimestamp/currentGasLimit/currentBaseFee/currentRandom)与
//     _op_expected.header(gasUsed/receiptsRoot/logsBloom/withdrawalsRoot/requestsHash/
//     blobGasUsed/stateRoot)——向量语料原生 15 个头字段(同 Task 2 report 自检 (a) 的
//     字段来源口径:只用向量自身字段 + golden 目录的 extraData/excessBlobGas/
//     transactionsRoot,不读 encodedHeaderHex 来"拼出" encode() 的输入)。
//   - t8n/golden/engine/<id>.golden.json 的 extraData(原样发射)/excessBlobGas/
//     transactionsRoot/blockHash/encodedHeaderHex/rawTransactions(Task 2 离线金值,
//     pinned op-geth 背书;rawTransactions 供 bcos-rpc 的 deposit 套件用)。
//   - 3 个协议常量(ommersHash=keccak256(rlp([]))、difficulty=0、nonce=8 零字节)——
//     post-merge/PoS 链头恒定值,向量语料本就不携带这三个字段,op-geth 自身也是硬编码
//     (core/types 的 EmptyUncleHash 等),不是"自算自证"。
//
// 未编译验证:本文件随 Task 3 一次性写就提交,未经 cmake/ctest 实际编译或运行(用户
// 指令:开发期跳过 FISCO 编译/测试运行)。正确性依据见 task-3-report.md:①手工 RLP
// 结构走查(逐字段核对 isthmus_transfer_basic 的 encodedHeaderHex);②独立 Python
// 复刻同一套字段映射/RLP 规则,对全部 33 条 golden + 39 笔 deposit 逐字节比对
// (33/33 header、33/33 hash、39/39 deposit 全过)——而非 ctest 绿灯。
//
// 本文件仅在 in-tree 构建(bcos-framework 目标存在)编译——bcos-codec 的 `codec`
// target 只在完整 CMake 树(顶层 add_subdirectory(bcos-codec))下存在,恰与
// bcos-framework 存在性同条件,见 test/CMakeLists.txt 的 if(TARGET bcos-framework)。

#include <bcos-codec/rlp/OpHeaderCodec.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using Json = nlohmann::json;
namespace fs = std::filesystem;
using bcos::Address;
using bcos::bytes;
using bcos::h2048;
using bcos::h256;
using bcos::h64;
using bcos::u256;
using bcos::codec::rlp::decodeOpHeader;
using bcos::codec::rlp::encodeOpHeader;
using bcos::codec::rlp::OpHeaderConst;
using bcos::codec::rlp::opHeaderHash;

namespace
{

// post-merge/PoS 协议常量(spec §5.1;三个字段向量语料不携带,详见文件头注释)。
const h256 kEmptyOmmersHash{bcos::fromHex(
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"})};
const h64 kPosNonce{bcos::fromHex(std::string{"0x0000000000000000"})};

const OpHeaderConst kOpHeaderConst{.ommersHash = kEmptyOmmersHash,
    .difficulty = bcos::u256(0),
    .nonce = kPosNonce};

Json loadJsonOrFail(const fs::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        ADD_FAILURE() << "cannot open " << path.string();
        return Json::object();
    }
    Json j;
    in >> j;
    return j;
}

h256 asH256(std::string const& hex)
{
    return h256{bcos::fromHex(hex)};
}
Address asAddress(std::string const& hex)
{
    return Address{bcos::fromHex(hex)};
}
u256 asU256(std::string const& hex)
{
    return bcos::fromBigQuantity(hex);
}
uint64_t asU64(std::string const& hex)
{
    return bcos::fromQuantity(hex);
}
bytes asBytes(std::string const& hex)
{
    return bcos::fromHex(hex);
}

// vectors/<id>.json 顶层是 {"_op_test_vectors": {...}, "<id>": {env, _op_expected, ...}}。
Json const& vectorBody(Json const& doc, std::string const& id)
{
    return doc.at(id);
}

// 组一枚 FISCO BlockHeader(18 字段,经 #5385 访问器)+ 3 个协议常量(经 kOpHeaderConst)。
// 字段来源口径同原 buildHeader:向量自身 env/_op_expected.header(15 字段)+ golden 目录的
// extraData/excessBlobGas/transactionsRoot(3 字段)。timestamp 按 FISCO 惯例存毫秒。
std::unique_ptr<bcostars::protocol::BlockHeaderImpl> buildHeader(
    Json const& vec, Json const& golden)
{
    auto const& env = vec.at("env");
    auto const& header = vec.at("_op_expected").at("header");

    auto h = std::make_unique<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(static_cast<bcos::protocol::BlockNumber>(
        asU64(env.at("currentNumber").get<std::string>())));
    h->setTimestamp(
        static_cast<int64_t>(asU64(env.at("currentTimestamp").get<std::string>())) * 1000);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = h->number() - 1,
        .blockHash = asH256(env.at("parentHash").get<std::string>())});
    h->setCoinbase(asAddress(env.at("currentCoinbase").get<std::string>()));
    h->setStateRoot(asH256(header.at("stateRoot").get<std::string>()));
    h->setTxsRoot(asH256(golden.at("transactionsRoot").get<std::string>()));
    h->setReceiptsRoot(asH256(header.at("receiptsRoot").get<std::string>()));
    const auto bloom = h2048{asBytes(header.at("logsBloom").get<std::string>())};
    h->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    h->setGasLimit(asU256(env.at("currentGasLimit").get<std::string>()));
    h->setGasUsed(asU256(header.at("gasUsed").get<std::string>()));
    h->setExtraData(asBytes(golden.at("extraData").get<std::string>()));
    h->setPrevRandao(asH256(env.at("currentRandom").get<std::string>()));
    h->setBaseFee(asU256(env.at("currentBaseFee").get<std::string>()));
    h->setWithdrawalsRoot(asH256(header.at("withdrawalsRoot").get<std::string>()));
    h->setBlobGasUsed(asU256(header.at("blobGasUsed").get<std::string>()));
    h->setExcessBlobGas(asU256(golden.at("excessBlobGas").get<std::string>()));
    h->setParentBeaconBlockRoot(asH256(env.at("parentBeaconBlockRoot").get<std::string>()));
    h->setRequestsHash(asH256(header.at("requestsHash").get<std::string>()));
    return h;
}

// 逐字节比较 bytesConstRef(span 无 operator==,审查用显式长度+逐字节避免误用)。
void expectSameBytes(bcos::bytesConstRef a, bcos::bytesConstRef b, std::string const& what)
{
    EXPECT_EQ(a.size(), b.size()) << what;
    if (a.size() == b.size())
    {
        EXPECT_TRUE(std::equal(a.begin(), a.end(), b.begin())) << what;
    }
}

// golden/engine/manifest.txt → 33 个 golden 文件名(去后缀即 id),过滤注释/空行/
// chained/ 子目录说明行(同 Task 2 report 自检 (d) 口径)。
std::vector<std::string> loadManifestIds()
{
    std::vector<std::string> ids;
    fs::path manifestPath = fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "manifest.txt";
    std::ifstream in(manifestPath);
    if (!in.is_open())
    {
        ADD_FAILURE() << "cannot open " << manifestPath.string();
        return ids;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        constexpr std::string_view kSuffix = ".golden.json";
        if (line.size() > kSuffix.size() &&
            line.compare(line.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
        {
            ids.push_back(line.substr(0, line.size() - kSuffix.size()));
        }
    }
    return ids;
}

struct GoldenSample
{
    Json vectorDoc;
    Json vector;
    Json golden;
    Json caseDoc;
};

GoldenSample loadSample(std::string const& id)
{
    GoldenSample s;
    s.vectorDoc = loadJsonOrFail(fs::path(OP_T8N_VECTORS_DIR) / (id + ".json"));
    s.vector = vectorBody(s.vectorDoc, id);
    s.golden = loadJsonOrFail(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / (id + ".golden.json"));
    s.caseDoc = loadJsonOrFail(fs::path(OP_T8N_CASES_DIR) / (id + ".in.json"));
    return s;
}

void expectHeaderMatchesGolden(std::string const& id)
{
    auto sample = loadSample(id);
    auto header = buildHeader(sample.vector, sample.golden);

    // encode 字段级断言先于 hash(裁定 C3)。
    const bytes encoded = encodeOpHeader(*header, kOpHeaderConst);
    const bytes expectedEncoded = asBytes(sample.golden.at("encodedHeaderHex").get<std::string>());
    EXPECT_EQ(encoded, expectedEncoded) << id << ": encodeOpHeader != golden.encodedHeaderHex";

    const h256 hash = opHeaderHash(*header, kOpHeaderConst);
    const h256 expectedHash = asH256(sample.golden.at("blockHash").get<std::string>());
    EXPECT_EQ(hash, expectedHash) << id << ": opHeaderHash != golden.blockHash";

    // decode 反向断言(终审 B4-1 打通读路后新增):golden 的 encodedHeaderHex 必须解回一枚
    // 与 buildHeader 逐字段相同的头,且再 encode 回同一批字节。这条把 decode 钉在 op-geth
    // 金值上——它是 s_number_2_header 读路的正确性锚,engine 的 timestamp 单调校验(以及日后
    // Holocene baseFee / gasLimit 变化率)都建立在它之上。
    // 断言给的是**字段级**比较而非仅 re-encode 相等:后者在"decode 把两个同类型字段读反了、
    // encode 又按同样的错序写回"时会假绿(相邻防线失效仍通过 —— spec §11)。
    bytes decodableCopy = expectedEncoded;
    bcostars::protocol::BlockHeaderImpl decoded;
    OpHeaderConst decodedConst;
    auto decodeError = decodeOpHeader(
        bcos::bytesRef(decodableCopy.data(), decodableCopy.size()), decoded, decodedConst);
    ASSERT_EQ(decodeError, nullptr) << id << ": decodeOpHeader rejected golden.encodedHeaderHex";
    // 3 个常量经 out-param 往返(无 tars 载体)。
    EXPECT_EQ(decodedConst.ommersHash, kOpHeaderConst.ommersHash) << id;
    EXPECT_EQ(decodedConst.difficulty, kOpHeaderConst.difficulty) << id;
    EXPECT_EQ(decodedConst.nonce, kOpHeaderConst.nonce) << id;
    // 18 个 tars 载体字段经访问器比较。
    // parentInfo.blockNumber:decode 写 number-1(与 rebuildOpEthHeader 一致,审查 F1),golden 全
    // 是 block 1,N-1=0,这里显式断言以钉死该约定,防止未来漂移。
    EXPECT_EQ(decoded.parentInfo().blockHash, header->parentInfo().blockHash) << id;
    EXPECT_EQ(decoded.parentInfo().blockNumber, header->parentInfo().blockNumber) << id;
    EXPECT_EQ(decoded.coinbase(), header->coinbase()) << id;
    EXPECT_EQ(decoded.stateRoot(), header->stateRoot()) << id;
    EXPECT_EQ(decoded.txsRoot(), header->txsRoot()) << id;
    EXPECT_EQ(decoded.receiptsRoot(), header->receiptsRoot()) << id;
    expectSameBytes(decoded.logsBloom(), header->logsBloom(), id);
    EXPECT_EQ(decoded.number(), header->number()) << id;
    EXPECT_EQ(decoded.gasLimit(), header->gasLimit()) << id;
    EXPECT_EQ(decoded.gasUsed(), header->gasUsed()) << id;
    EXPECT_EQ(decoded.timestamp(), header->timestamp()) << id;
    expectSameBytes(decoded.extraData(), header->extraData(), id);
    EXPECT_EQ(decoded.prevRandao(), header->prevRandao()) << id;
    EXPECT_EQ(decoded.baseFee().value(), header->baseFee().value()) << id;
    EXPECT_EQ(decoded.withdrawalsRoot().value(), header->withdrawalsRoot().value()) << id;
    EXPECT_EQ(decoded.blobGasUsed().value(), header->blobGasUsed().value()) << id;
    EXPECT_EQ(decoded.excessBlobGas().value(), header->excessBlobGas().value()) << id;
    EXPECT_EQ(decoded.parentBeaconBlockRoot().value(), header->parentBeaconBlockRoot().value())
        << id;
    EXPECT_EQ(decoded.requestsHash().value(), header->requestsHash().value()) << id;
    EXPECT_EQ(encodeOpHeader(decoded, decodedConst), expectedEncoded)
        << id << ": decodeOpHeader()->encodeOpHeader is not identity";
}

}  // namespace

// ─────────────────────────── EthBlockHeader 套件 ───────────────────────────

// Step 1(TDD 三选样之一):Isthmus 单笔(attributes deposit + 1 笔 EIP-1559 转账)。
TEST(EthBlockHeader, IsthmusSingleTxTransferBasic)
{
    expectHeaderMatchesGolden("isthmus_transfer_basic");
}

// Step 1(TDD 三选样之二):Jovian 多笔(非平凡 trie,extraData 17B 含 minBaseFee)。
TEST(EthBlockHeader, JovianMultiTxTransferMulti)
{
    expectHeaderMatchesGolden("jovian_transfer_multi");
}

// Step 1(TDD 三选样之三):deposit-only(仅 attributes deposit,无用户交易)。
TEST(EthBlockHeader, DepositOnlyIsthmus)
{
    expectHeaderMatchesGolden("isthmus_deposit_only");
}

// Step 3:33 条全量,encode()/hash() 双断言逐条比对(spec §8 验收清单同款覆盖面)。
TEST(EthBlockHeader, AllThirtyThreeGoldenVectors)
{
    auto ids = loadManifestIds();
    ASSERT_EQ(ids.size(), 33U) << "manifest.txt id count drifted from Task 2's 33-vector corpus";
    for (auto const& id : ids)
    {
        SCOPED_TRACE(id);
        expectHeaderMatchesGolden(id);
    }
}
