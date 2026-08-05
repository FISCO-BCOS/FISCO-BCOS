// bcos-rpc/test/unittests/rpc/OpDepositEncodeTest.cpp
// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
//
// OP 0x7E deposit envelope 的 golden 逐字节比对套件。2026-08-05 自 bcos-evm 的
// EthBlockHeaderTest.cpp 迁来:编码内联进了 bcos-rpc 的 DepositTxHandler(TxHandler.cpp),
// 随退役的 bcos-codec/rlp/OpDepositEncode.{h,cpp} 一起——golden 覆盖跟着编码器走。
//
// 编码器:Web3Transaction::encode() → handlerFor(Deposit).encode() → DepositTxHandler::encode,
// 即 takeToTarsTransaction 提交路径的同一入口(该路径存 extraTransactionBytes 于 tars struct)。
// 金值全部来自 Task 2 的产物(op-geth 离线背书),不自算:
//   - t8n/cases/<id>.in.json 的 transactions[]._op_deposit:结构化 deposit 字段;
//   - t8n/golden/engine/<id>.golden.json 的 rawTransactions[i]:逐字节金值。
//
// 语料路径经 bcos-rpc/test/CMakeLists.txt 的 OP_T8N_CASES_DIR/OP_T8N_GOLDEN_ENGINE_DIR
// 编译定义注入(共享 bcos-evm 的 opstack/t8n 语料);本文件仅在 in-tree 构建编译。

#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <bcos-rpc/web3jsonrpc/model/TxHandler.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using bcos::Address;
using bcos::bytes;
using bcos::h256;
using bcos::u256;
using bcos::rpc::Web3Transaction;

namespace
{

// 助手函数统一 op 前缀:test-bcos-rpc 开 UNITY_BUILD,多个测试 TU 合并为一个,裸名
// (如 asBytes,Web3RpcTest.cpp 亦有)会在此 TU 内撞名/歧义。

Json::Value opLoadJsonOrFail(fs::path const& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        BOOST_FAIL("cannot open " + path.string());
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, in, &root, nullptr))
    {
        BOOST_FAIL("cannot parse " + path.string());
    }
    return root;
}

h256 opAsH256(std::string const& hex)
{
    return h256{bcos::fromHex(hex)};
}
Address opAsAddress(std::string const& hex)
{
    return Address{bcos::fromHex(hex)};
}
u256 opAsU256(std::string const& hex)
{
    return bcos::fromBigQuantity(hex);
}
uint64_t opAsU64(std::string const& hex)
{
    return bcos::fromQuantity(hex);
}
bytes opAsBytes(std::string const& hex)
{
    return bcos::fromHex(hex);
}

// golden/engine/manifest.txt → 33 个 golden 文件名(去后缀即 id),过滤注释/空行。
std::vector<std::string> opLoadManifestIds()
{
    std::vector<std::string> ids;
    fs::path manifestPath = fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "manifest.txt";
    std::ifstream in(manifestPath);
    if (!in.is_open())
    {
        BOOST_FAIL("cannot open " + manifestPath.string());
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

// cases/<id>.in.json 的一枚 `_op_deposit` 结构化块 → Deposit 类型 Web3Transaction。字段映射
// 即 DepositTxHandler::encode 的输入面(与 Web3Transaction::takeToTarsTransaction 的
// deposit 分支同一字段集)。39 笔 deposit tx 里 37 笔 `to` 存在,2 笔
// (isthmus_contract_create/jovian_contract_create 的 tx index 1)`to` 为 JSON null——真实
// contract-creation deposit 样本,`to` nullopt → RLP 空串分支确有金值覆盖,非纯理论分支。
Web3Transaction opBuildDepositTransaction(Json::Value const& tx)
{
    auto const& dep = tx["_op_deposit"];
    Web3Transaction w3{};
    // 全限定:unity build 下另一 TU 的 `using namespace bcos;` 会把
    // bcos::protocol::TransactionType 引入全局作用域,与 bcos::rpc::TransactionType 撞名。
    w3.type = bcos::rpc::TransactionType::Deposit;
    w3.sourceHash = opAsH256(dep["source_hash"].asString());
    w3.from = opAsAddress(dep["from"].asString());
    if (dep.isMember("to") && !dep["to"].isNull())
    {
        w3.to = opAsAddress(dep["to"].asString());
    }
    if (dep.isMember("mint"))
    {
        w3.mint = opAsU256(dep["mint"].asString());
    }
    if (dep.isMember("value"))
    {
        w3.value = opAsU256(dep["value"].asString());
    }
    w3.gasLimit = opAsU64(dep["gas"].asString());
    w3.isSystemTx = dep["is_system_tx"].asBool();
    if (tx.isMember("data"))
    {
        w3.data = opAsBytes(tx["data"].asString());
    }
    return w3;
}

}  // namespace

// 单条 deposit 的 encode 结果与 golden.rawTransactions 同下标比对。
// 下标用 Json::ArrayIndex(而非 std::size_t):此版本 JsonCpp 对 operator[] 的 ArrayIndex/int
// 双重载使 unsigned long 歧义。
void opExpectDepositMatchesGolden(
    Json::Value const& txs, Json::Value const& rawTransactions, Json::ArrayIndex i)
{
    auto const& tx = txs[i];
    BOOST_REQUIRE_EQUAL(tx["_op_type"].asString(), "deposit");
    auto w3 = opBuildDepositTransaction(tx);
    auto encoded = w3.encode();
    auto expected = opAsBytes(rawTransactions[i].asString());
    BOOST_CHECK_MESSAGE(encoded == expected, "deposit tx index " << i);
}

// isthmus_transfer_basic 的 attributes deposit(index 0,无 mint/value,`to` 为既有系统合约地址)。
BOOST_AUTO_TEST_CASE(opDepositEncode_isthmus_transfer_basic)
{
    auto caseDoc = opLoadJsonOrFail(fs::path(OP_T8N_CASES_DIR) / "isthmus_transfer_basic.in.json");
    auto golden = opLoadJsonOrFail(
        fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "isthmus_transfer_basic.golden.json");
    opExpectDepositMatchesGolden(caseDoc["transactions"], golden["rawTransactions"], 0);
}

// mint/value 两个可选字段同时在场的分支(isthmus_deposit_mint index 1)——覆盖
// mint/value 非零路径。
BOOST_AUTO_TEST_CASE(opDepositEncode_mint_and_value)
{
    auto caseDoc = opLoadJsonOrFail(fs::path(OP_T8N_CASES_DIR) / "isthmus_deposit_mint.in.json");
    auto golden = opLoadJsonOrFail(
        fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "isthmus_deposit_mint.golden.json");
    auto const& tx = caseDoc["transactions"][Json::ArrayIndex(1)];
    BOOST_REQUIRE_EQUAL(tx["_op_type"].asString(), "deposit");
    BOOST_REQUIRE(tx["_op_deposit"].isMember("mint"));
    BOOST_REQUIRE(tx["_op_deposit"].isMember("value"));
    opExpectDepositMatchesGolden(caseDoc["transactions"], golden["rawTransactions"], 1);
}

// `to` == JSON null: real contract-creation deposit (isthmus_contract_create index 1)——
// exercise DepositTxHandler 的 `to` nullopt 分支,必须编码为空串而非 20 字节零地址。
BOOST_AUTO_TEST_CASE(opDepositEncode_contract_creation_nil_to)
{
    auto caseDoc = opLoadJsonOrFail(fs::path(OP_T8N_CASES_DIR) / "isthmus_contract_create.in.json");
    auto golden = opLoadJsonOrFail(
        fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "isthmus_contract_create.golden.json");
    auto const& tx = caseDoc["transactions"][Json::ArrayIndex(1)];
    BOOST_REQUIRE_EQUAL(tx["_op_type"].asString(), "deposit");
    BOOST_REQUIRE(tx["_op_deposit"].isMember("to"));
    BOOST_REQUIRE(tx["_op_deposit"]["to"].isNull());
    opExpectDepositMatchesGolden(caseDoc["transactions"], golden["rawTransactions"], 1);
}

// 33 条向量的全部 deposit 交易逐字节重建,与 golden.rawTransactions 对应下标比对。
// 断言重建计数恰为 39(Task 2 report 计数),防止 `_op_type=="deposit"` 判据静默漏判
// 导致循环体空跑却仍然"绿"。
BOOST_AUTO_TEST_CASE(opDepositEncode_all_deposit_transactions)
{
    auto ids = opLoadManifestIds();
    BOOST_REQUIRE_EQUAL(ids.size(), 33U);

    std::size_t depositCount = 0;
    for (auto const& id : ids)
    {
        auto caseDoc = opLoadJsonOrFail(fs::path(OP_T8N_CASES_DIR) / (id + ".in.json"));
        auto golden =
            opLoadJsonOrFail(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / (id + ".golden.json"));
        auto const& txs = caseDoc["transactions"];
        auto const& rawTransactions = golden["rawTransactions"];
        BOOST_REQUIRE_MESSAGE(txs.size() == rawTransactions.size(),
            id << ": case transaction count != golden.rawTransactions count");

        for (Json::ArrayIndex i = 0; i < txs.size(); ++i)
        {
            if (txs[i]["_op_type"].asString() != "deposit")
            {
                continue;
            }
            ++depositCount;
            opExpectDepositMatchesGolden(txs, rawTransactions, i);
        }
    }
    BOOST_CHECK_MESSAGE(depositCount == 39U,
        "expected 39 deposit transactions across the 33-vector corpus (Task 2 report count); "
        "a different count means either the corpus changed or `_op_type` matching regressed");
}
