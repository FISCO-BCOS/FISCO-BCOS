// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EthBlockHeaderTest.cpp — 闭环 Task 3(design §5.1/§7.5,决策 C3/C6)。
//
// 覆盖两个独立 GTest 套件(brief 裁定 C6,套件名钉死):
//   - EthBlockHeader:      bcos::codec::rlp::EthBlockHeader::encode()/hash()
//   - OpDepositEncode:     bcos::codec::rlp::encodeDepositEnvelope()
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
//     pinned op-geth 背书)。
//   - t8n/cases/<id>.in.json 的 transactions[].{_op_type=="deposit" ? _op_deposit : ...}
//     结构化 deposit 字段,用于喂 encodeDepositEnvelope()。
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

#include <bcos-codec/rlp/EthBlockHeader.h>
#include <bcos-codec/rlp/OpDepositEncode.h>
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
using bcos::codec::rlp::encodeDepositEnvelope;
using bcos::codec::rlp::EthBlockHeader;
using bcos::codec::rlp::OpDepositFields;

namespace
{

// post-merge/PoS 协议常量(spec §5.1;三个字段向量语料不携带,详见文件头注释)。
const h256 kEmptyOmmersHash{bcos::fromHex(
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"})};
const h64 kPosNonce{bcos::fromHex(std::string{"0x0000000000000000"})};

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

// 组一枚 EthBlockHeader:向量自身 env/_op_expected.header(15 字段)+ golden 目录的
// extraData/excessBlobGas/transactionsRoot(3 字段)+ 3 个协议常量。字段来源口径同
// Task 2 report 自检 (a)。
EthBlockHeader buildHeader(Json const& vec, Json const& golden)
{
    auto const& env = vec.at("env");
    auto const& header = vec.at("_op_expected").at("header");

    EthBlockHeader h;
    h.parentHash = asH256(env.at("parentHash").get<std::string>());
    h.ommersHash = kEmptyOmmersHash;
    h.feeRecipient = asAddress(env.at("currentCoinbase").get<std::string>());
    h.stateRoot = asH256(header.at("stateRoot").get<std::string>());
    h.transactionsRoot = asH256(golden.at("transactionsRoot").get<std::string>());
    h.receiptsRoot = asH256(header.at("receiptsRoot").get<std::string>());
    h.logsBloom = h2048{asBytes(header.at("logsBloom").get<std::string>())};
    h.difficulty = u256(0);
    h.number = asU64(env.at("currentNumber").get<std::string>());
    h.gasLimit = asU64(env.at("currentGasLimit").get<std::string>());
    h.gasUsed = asU64(header.at("gasUsed").get<std::string>());
    h.timestamp = asU64(env.at("currentTimestamp").get<std::string>());
    h.extraData = asBytes(golden.at("extraData").get<std::string>());
    h.prevRandao = asH256(env.at("currentRandom").get<std::string>());
    h.nonce = kPosNonce;
    h.baseFeePerGas = asU256(env.at("currentBaseFee").get<std::string>());
    h.withdrawalsRoot = asH256(header.at("withdrawalsRoot").get<std::string>());
    h.blobGasUsed = asU64(header.at("blobGasUsed").get<std::string>());
    h.excessBlobGas = asU64(golden.at("excessBlobGas").get<std::string>());
    h.parentBeaconBlockRoot = asH256(env.at("parentBeaconBlockRoot").get<std::string>());
    h.requestsHash = asH256(header.at("requestsHash").get<std::string>());
    return h;
}

// cases/<id>.in.json 的一枚 `_op_deposit` 结构化块 → OpDepositFields。字段名与向量语料
// 一一对齐(brief:"结构化字段,签名对齐向量 _op_deposit")。39 笔 deposit tx 里 37 笔
// `to` 存在,2 笔(isthmus_contract_create/jovian_contract_create 的 tx index 1)
// `to` 为 JSON null——真实 contract-creation deposit 样本,`OpDepositFields::to` 的
// optional/nil 分支确有金值覆盖,非纯理论分支(见下方 is_null() 处理)。
OpDepositFields buildDepositFields(Json const& tx)
{
    auto const& dep = tx.at("_op_deposit");
    OpDepositFields f;
    f.sourceHash = asH256(dep.at("source_hash").get<std::string>());
    f.from = asAddress(dep.at("from").get<std::string>());
    // `to` is JSON `null` (present-but-null, not absent) for the two contract-creation
    // deposits in this corpus (isthmus_contract_create/jovian_contract_create, tx index 1) —
    // `contains("to")` alone is true either way, so it must be paired with `!is_null()`.
    if (dep.contains("to") && !dep.at("to").is_null())
    {
        f.to = asAddress(dep.at("to").get<std::string>());
    }
    if (dep.contains("mint"))
    {
        f.mint = asU256(dep.at("mint").get<std::string>());
    }
    if (dep.contains("value"))
    {
        f.value = asU256(dep.at("value").get<std::string>());
    }
    f.gas = asU64(dep.at("gas").get<std::string>());
    f.isSystemTransaction = dep.at("is_system_tx").get<bool>();
    if (tx.contains("data"))
    {
        f.data = asBytes(tx.at("data").get<std::string>());
    }
    return f;
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
    EthBlockHeader header = buildHeader(sample.vector, sample.golden);

    // encode() 字段级断言先于 hash()(brief/spec §7.5 决议 C3)。
    const bytes encoded = header.encode();
    const bytes expectedEncoded = asBytes(sample.golden.at("encodedHeaderHex").get<std::string>());
    EXPECT_EQ(encoded, expectedEncoded) << id << ": encode() != golden.encodedHeaderHex";

    const h256 hash = header.hash();
    const h256 expectedHash = asH256(sample.golden.at("blockHash").get<std::string>());
    EXPECT_EQ(hash, expectedHash) << id << ": hash() != golden.blockHash";
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

// ─────────────────────────── OpDepositEncode 套件 ──────────────────────────

// Step 1(TDD deposit 腿):isthmus_transfer_basic 的 attributes deposit(index 0,无
// mint/value,`to` 为既有系统合约地址)。
TEST(OpDepositEncode, IsthmusTransferBasicDepositEnvelope)
{
    auto sample = loadSample("isthmus_transfer_basic");
    auto const& tx = sample.caseDoc.at("transactions").at(0);
    ASSERT_EQ(tx.at("_op_type").get<std::string>(), "deposit");

    const bytes encoded = encodeDepositEnvelope(buildDepositFields(tx));
    const bytes expected = asBytes(sample.golden.at("rawTransactions").at(0).get<std::string>());
    EXPECT_EQ(encoded, expected);
}

// mint/value 两个可选字段同时在场的分支(isthmus_deposit_mint index 1)——覆盖
// OpDepositFields::mint/value 非默认值路径。
TEST(OpDepositEncode, DepositWithMintAndValue)
{
    auto sample = loadSample("isthmus_deposit_mint");
    auto const& tx = sample.caseDoc.at("transactions").at(1);
    ASSERT_EQ(tx.at("_op_type").get<std::string>(), "deposit");
    ASSERT_TRUE(tx.at("_op_deposit").contains("mint"));
    ASSERT_TRUE(tx.at("_op_deposit").contains("value"));

    const bytes encoded = encodeDepositEnvelope(buildDepositFields(tx));
    const bytes expected = asBytes(sample.golden.at("rawTransactions").at(1).get<std::string>());
    EXPECT_EQ(encoded, expected);
}

// `to` == JSON null: real contract-creation deposit (isthmus_contract_create index 1) —
// exercises OpDepositFields::to == nullopt, which must RLP-encode as the empty string, not as
// a present-but-zero 20-byte address. This is a genuine golden sample, not a synthetic one.
TEST(OpDepositEncode, ContractCreationDepositNilTo)
{
    auto sample = loadSample("isthmus_contract_create");
    auto const& tx = sample.caseDoc.at("transactions").at(1);
    ASSERT_EQ(tx.at("_op_type").get<std::string>(), "deposit");
    ASSERT_TRUE(tx.at("_op_deposit").at("to").is_null());

    const OpDepositFields fields = buildDepositFields(tx);
    ASSERT_FALSE(fields.to.has_value());

    const bytes encoded = encodeDepositEnvelope(fields);
    const bytes expected = asBytes(sample.golden.at("rawTransactions").at(1).get<std::string>());
    EXPECT_EQ(encoded, expected);
}

// Step 3(closes Task 2 自检 (b) 的 deposit 半部):33 条向量的全部 deposit 交易逐字节
// 重建,与 golden.rawTransactions 对应下标比对。T2 报告记录 167 笔非 deposit 已比对
// (167/167 match),deposit 39 笔留待本任务——这里断言重建计数恰为 39,防止
// `_op_type == "deposit"` 判据静默漏判导致循环体空跑却仍然"绿"。
TEST(OpDepositEncode, AllDepositTransactionsAcrossThirtyThreeGoldenVectors)
{
    auto ids = loadManifestIds();
    ASSERT_EQ(ids.size(), 33U);

    std::size_t depositCount = 0;
    for (auto const& id : ids)
    {
        SCOPED_TRACE(id);
        auto sample = loadSample(id);
        auto const& txs = sample.caseDoc.at("transactions");
        auto const& rawTransactions = sample.golden.at("rawTransactions");
        ASSERT_EQ(txs.size(), rawTransactions.size())
            << id << ": case transaction count != golden.rawTransactions count";

        for (std::size_t i = 0; i < txs.size(); ++i)
        {
            auto const& tx = txs.at(i);
            if (tx.at("_op_type").get<std::string>() != "deposit")
            {
                continue;
            }
            ++depositCount;
            const bytes encoded = encodeDepositEnvelope(buildDepositFields(tx));
            const bytes expected = asBytes(rawTransactions.at(i).get<std::string>());
            EXPECT_EQ(encoded, expected) << id << ": deposit tx index " << i;
        }
    }
    // Task 2 report: "deposit 交易(39 笔)" across the 33-vector corpus.
    EXPECT_EQ(depositCount, 39U)
        << "expected 39 deposit transactions across the 33-vector corpus (Task 2 report count); "
           "a different count means either the corpus changed or `_op_type` matching regressed";
}
