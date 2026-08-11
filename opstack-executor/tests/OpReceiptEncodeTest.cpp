#include "OpTestReceiptFactory.h"
#include "TestPrinters.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpBlockSeal.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <sstream>
#include <test/utils/rlp.hpp>
#include <test/utils/rlp_encode.hpp>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmc::literals;

namespace
{
/// A deposit receipt projected onto the FISCO receipt: cumulative "0x5208" (21000), 256-zero
/// bloom, deposit_nonce/version in opStackMeta. `status` is the FISCO status (0 = success).
bcos::protocol::TransactionReceipt::Ptr minimalDepositReceipt(int32_t status = 0)
{
    auto r = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, status, bcos::bytesConstRef{}, /*blockNumber=*/1);
    r->setCumulativeGasUsed("0x5208");
    bcos::bytes bloom(256, 0x00);
    r->setLogsBloom(bcos::ref(bloom));
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 5;
    meta.deposit_receipt_version = 1;
    r->setOpStackMeta(std::move(meta));
    return r;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpReceiptEncodeSuite)

// 手工逐字节推导的 golden fixture（断言数值纪律：推导即锚，最终字节权威归 M-B3 差分）。
// RLP 列表项：status 成功 → 0x01（1B）；cumGas 21000=0x5208 → 0x82 52 08（3B）；
// bloom 256 零字节 → 0xb9 0x0100 + 00×256（259B）；logs [] → 0xc0（1B）；
// nonce 5 → 0x05（1B）；version 1 → 0x01（1B）。载荷 = 1+3+259+1+1+1 = 266 = 0x010a
// → 列表头 0xf9 01 0a（3B）。前缀 0x7e。总长 1+3+266 = 270。
BOOST_AUTO_TEST_CASE(DepositGoldenBytes)
{
    const auto enc =
        encodeReceiptForRoot(*minimalDepositReceipt(), static_cast<uint8_t>(kDepositTxType));
    evmc::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x01, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected += evmc::bytes(256, 0x00);
    expected += evmc::bytes{0xc0, 0x05, 0x01};
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// 失败 deposit：status 项 = 空串 0x80（op-geth statusEncoding 失败分支）。
// rev.2 勘正：0x80 与成功的 0x01 同为 1 字节（RLP 单字节优化对空串产 0x80 无内容），
// 载荷不变仍 266 = 0x010a、总长仍 270——仅第 5 字节 0x01→0x80。
BOOST_AUTO_TEST_CASE(FailedDepositStatusIsEmptyString)
{
    auto dep = minimalDepositReceipt(/*status=*/1);  // FISCO 非 0 = 失败
    const auto enc = encodeReceiptForRoot(*dep, static_cast<uint8_t>(kDepositTxType));
    evmc::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x80, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected += evmc::bytes(256, 0x00);
    expected += evmc::bytes{0xc0, 0x05, 0x01};
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// 带 log 的 deposit：logs 段（含 vector 列表 wrapper）以 evmone 独立编码为锚整段比对；
// 尾 2 字节 = {nonce, version}。总长关系锚（rev.2 红队补强，堵 wrapper 丢失盲区）：
// 空 logs 版总长 270，其中 logs 项占 1 字节（0xc0）→ 总长 = 269 + |rlp(logs)|
// （bloom 编码长度与内容无关恒 259；nonce 7 与 5 同为 1 字节）。
BOOST_AUTO_TEST_CASE(DepositWithLogEmbedsEncodedLogsAndNonceTail)
{
    constexpr auto kAddr = 0x00000000000000000000000000000000000000aa_address;
    evmone::state::Log evmoneLog{
        .addr = kAddr, .data = evmc::bytes{0x68, 0x69}, .topics = {0x01_bytes32}};

    // Project the log onto a FISCO LogEntry (raw bytes, same mapping mapOpLogs uses).
    evmc::bytes32 topicVal = 0x01_bytes32;
    std::vector<bcos::protocol::LogEntry> logs;
    logs.emplace_back(bcos::bytes(kAddr.bytes, kAddr.bytes + sizeof(kAddr.bytes)),
        bcos::h256s{bcos::h256(topicVal.bytes, sizeof(topicVal.bytes))}, bcos::bytes{0x68, 0x69});
    auto dep = kOpTestReceiptFactory->createReceipt(
        bcos::u256(21000), std::string{}, logs, /*status=*/0, bcos::bytesConstRef{}, 1);
    dep->setCumulativeGasUsed("0x5208");
    const auto bloomF =
        evmone::state::compute_bloom_filter(std::span<const evmone::state::Log>{&evmoneLog, 1});
    bcos::bytes bloom(bloomF.bytes, bloomF.bytes + sizeof(bloomF.bytes));
    dep->setLogsBloom(bcos::ref(bloom));
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 7;
    meta.deposit_receipt_version = 1;
    dep->setOpStackMeta(std::move(meta));

    const auto enc = encodeReceiptForRoot(*dep, static_cast<uint8_t>(kDepositTxType));
    BOOST_CHECK_EQUAL(enc[0], 0x7e);
    // evmone 的 vector<Log> 列表编码（独立路径）须整段出现——覆盖列表 wrapper 字节
    const auto logsBytes = evmone::rlp::encode(std::vector<evmone::state::Log>{evmoneLog});
    BOOST_CHECK_NE(enc.find(logsBytes), evmc::bytes::npos);
    BOOST_REQUIRE_EQUAL(enc.size(), 269u + logsBytes.size());
    BOOST_CHECK_EQUAL(enc[enc.size() - 2], 0x07);  // nonce
    BOOST_CHECK_EQUAL(enc[enc.size() - 1], 0x01);  // version
}

// 普通 tx：逐字节等价于 evmone rlp_encode（含 typed 前缀）——由 FISCO receipt 重建，
// 且必须与 evmone 对同形 evmone receipt 的编码一致。
BOOST_AUTO_TEST_CASE(NormalReceiptMatchesEvmoneEncoding)
{
    // evmone reference: eip1559, success, cumGas 42000, empty logs, zero bloom.
    evmone::state::TransactionReceipt ref;
    ref.type = evmone::state::Transaction::Type::eip1559;
    ref.status = EVMC_SUCCESS;
    ref.cumulative_gas_used = 42000;
    const auto expected = evmone::state::rlp_encode(ref);

    auto r = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/1);
    r->setCumulativeGasUsed("0xa410");  // 42000
    bcos::bytes bloom(256, 0x00);
    r->setLogsBloom(bcos::ref(bloom));
    const auto enc =
        encodeReceiptForRoot(*r, static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));

    BOOST_CHECK_EQUAL(enc[0], 0x02);  // eip1559 typed 前缀
    BOOST_CHECK_EQUAL(enc, evmc::bytes(expected.begin(), expected.end()));
}

// legacy（无 typed 前缀）+ access_list 前缀亦与 evmone 逐字节一致
BOOST_AUTO_TEST_CASE(NormalReceiptLegacyAndAccessListPrefixes)
{
    const auto makeFisco = [](uint64_t cumGas) {
        auto r = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
            std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
        std::ostringstream oss;
        oss << "0x" << std::hex << cumGas;
        r->setCumulativeGasUsed(oss.str());
        bcos::bytes bloom(256, 0x00);
        r->setLogsBloom(bcos::ref(bloom));
        return r;
    };
    const auto compareToEvmone = [](const auto& fisco, evmone::state::Transaction::Type type) {
        evmone::state::TransactionReceipt ref;
        ref.type = type;
        ref.status = EVMC_SUCCESS;
        ref.cumulative_gas_used = 42000;
        const auto expected = evmone::state::rlp_encode(ref);
        const auto enc = encodeReceiptForRoot(*fisco, static_cast<uint8_t>(type));
        BOOST_CHECK_EQUAL(enc, evmc::bytes(expected.begin(), expected.end()));
    };

    auto legacy = makeFisco(42000);
    compareToEvmone(legacy, evmone::state::Transaction::Type::legacy);
    auto accessList = makeFisco(42000);
    compareToEvmone(accessList, evmone::state::Transaction::Type::access_list);
}

// deposit 与普通 tx 必须产出不同叶子（前缀与尾部字段都不同）
BOOST_AUTO_TEST_CASE(DepositAndNormalLeavesDiffer)
{
    const auto depEnc =
        encodeReceiptForRoot(*minimalDepositReceipt(), static_cast<uint8_t>(kDepositTxType));
    auto normal = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    normal->setCumulativeGasUsed("0x5208");
    bcos::bytes bloom(256, 0x00);
    normal->setLogsBloom(bcos::ref(bloom));
    const auto normalEnc = encodeReceiptForRoot(
        *normal, static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    BOOST_CHECK_NE(depEnc, normalEnc);
}

// 端到端 receiptsRoot 等价（33 向量 gate 的叶子层证明）：从 FISCO receipts 用
// sealOpBlock 建根，必须与「等价 evmone receipts 用 evmone rlp_encode 建根」逐位一致。
// sealOpBlock 的三棵子树里只有 receiptsRoot 的叶子编码在此重构（trie 构造原样），所以
// 这一条 + NormalReceiptMatchesEvmoneEncoding 合起来钉住 receiptsRoot 字节等价。
BOOST_AUTO_TEST_CASE(SealReceiptsRootMatchesEvmoneReferenceTrie)
{
    // FISCO receipt #0: deposit (status 0, cumGas 21000, nonce 5, version 1).
    auto dep = minimalDepositReceipt();
    // FISCO receipt #1: eip1559 (status 0, cumGas 42000).
    auto normal = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    normal->setCumulativeGasUsed("0xa410");  // 42000
    bcos::bytes bloom(256, 0x00);
    normal->setLogsBloom(bcos::ref(bloom));

    bcos::evm::opstack::OpBlockResult result;
    result.receipts.push_back(dep);
    result.receipts.push_back(normal);
    result.txTypes.push_back(static_cast<uint8_t>(kDepositTxType));
    result.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));

    const auto seal =
        bcos::evm::opstack::sealOpBlock(result, bcos::evm::opstack::isthmusConfig(), {});

    // Reference: equivalent evmone receipts, same leaves via evmone rlp_encode, same trie.
    evmone::state::TransactionReceipt refDep;
    refDep.type = kDepositTxType;
    refDep.status = EVMC_SUCCESS;
    refDep.cumulative_gas_used = 21000;
    refDep.logs_bloom_filter = evmone::state::BloomFilter{};
    const auto depLeaf = evmc::bytes{0x7e} + evmone::rlp::encode_tuple(bool{true}, uint64_t{21000},
                                                 evmone::bytes_view(refDep.logs_bloom_filter),
                                                 refDep.logs, uint64_t{5}, uint64_t{1});

    evmone::state::TransactionReceipt refNormal;
    refNormal.type = evmone::state::Transaction::Type::eip1559;
    refNormal.status = EVMC_SUCCESS;
    refNormal.cumulative_gas_used = 42000;
    refNormal.logs_bloom_filter = evmone::state::BloomFilter{};
    const auto normalLeaf = evmone::state::rlp_encode(refNormal);

    std::vector<std::pair<bcos::bytes, bcos::bytes>> refEntries;
    for (size_t i = 0; i < 2; ++i)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, static_cast<uint64_t>(i));
        const auto& leaf = (i == 0) ? depLeaf : normalLeaf;
        refEntries.emplace_back(std::move(key), bcos::bytes{leaf.begin(), leaf.end()});
    }
    const auto refRoot = bcos::ledger::mpt::computeTrieRootVarKey(refEntries);

    bcos::h256 actual(seal.receiptsRoot.bytes, sizeof(seal.receiptsRoot.bytes));
    bcos::h256 expected;
    std::memcpy(expected.mutableData().data(), refRoot.root.data(), sizeof(expected));
    BOOST_CHECK_EQUAL(actual, expected);
}

BOOST_AUTO_TEST_SUITE_END()
