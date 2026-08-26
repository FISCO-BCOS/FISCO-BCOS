#include "OpTestReceiptFactory.h"
#include "TestPrinters.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <opstack-executor/OpBlockExecute.h>  // encodeReceiptForRoot (seal merged here)
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <limits>
#include <map>
#include <sstream>
#include <test/utils/rlp.hpp>
#include <test/utils/rlp_encode.hpp>
#include <utility>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmc::literals;

namespace
{
/// A deposit receipt projected onto the FISCO receipt: cumulative gas 21000, 256-zero bloom,
/// deposit_nonce/version in opStackMeta. `status` is the FISCO status (0 = success).
bcos::protocol::TransactionReceipt::Ptr minimalDepositReceipt(
    int32_t status = 0, std::string cumulativeGasUsed = "0x5208")
{
    auto r = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, status, bcos::bytesConstRef{}, /*blockNumber=*/1);
    r->setCumulativeGasUsed(std::move(cumulativeGasUsed));
    bcos::bytes bloom(256, 0x00);
    r->setLogsBloom(bcos::ref(bloom));
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 5;
    meta.deposit_receipt_version = 1;
    r->setOpStackMeta(std::move(meta));
    return r;
}

/// Receipt-suite exception pin. Measured on this binary (links protocol-tars / bcos-crypto /
/// ledger, i.e. the wedprcrypto closure): `catch (const OpConsensusError&)` binds. A
/// catch(...) that treats any throw as success would false-green these consensus guards.
inline auto consensusWhatContains(std::string_view needle)
{
    return [needle](bcos::evm::OpConsensusError const& e) {
        return std::string(e.what()).find(std::string{needle}) != std::string::npos;
    };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpReceiptEncodeSuite)

// Hand-derived golden fixture byte-by-byte (assertion-numerics discipline: derivation is
// the anchor; final byte authority belongs to M-B3 differential). RLP list items:
// status success -> 0x01 (1B); cumGas 21000=0x5208 -> 0x82 52 08 (3B); bloom 256 zero
// bytes -> 0xb9 0x0100 + 00x256 (259B); logs [] -> 0xc0 (1B); nonce 5 -> 0x05 (1B);
// version 1 -> 0x01 (1B). Payload = 1+3+259+1+1+1 = 266 = 0x010a -> list header 0xf9 01 0a
// (3B). Prefix 0x7e. Total 1+3+266 = 270.
BOOST_AUTO_TEST_CASE(DepositGoldenBytes)
{
    const auto enc =
        encodeReceiptForRoot(*minimalDepositReceipt(), static_cast<uint8_t>(kDepositTxType));
    bcos::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x01, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected.insert(expected.end(), 256, 0x00);
    expected.insert(expected.end(), {0xc0, 0x05, 0x01});
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// Production stores cumulativeGasUsed as decimal, while historical receipts may carry a 0x
// quantity. Both representations must commit to exactly the same receipt leaf.
BOOST_AUTO_TEST_CASE(DepositDecimalAndHexCumulativeGasAreEquivalent)
{
    const auto decimal = encodeReceiptForRoot(
        *minimalDepositReceipt(0, "21000"), static_cast<uint8_t>(kDepositTxType));
    const auto hex = encodeReceiptForRoot(
        *minimalDepositReceipt(0, "0x5208"), static_cast<uint8_t>(kDepositTxType));
    BOOST_CHECK_EQUAL(decimal, hex);
}

// Failed deposit: status item = empty string 0x80 (op-geth statusEncoding failure branch).
// rev.2 correction: 0x80 and the success 0x01 are both 1 byte (RLP single-byte
// optimization yields 0x80 for the empty string, no content); payload stays 266 = 0x010a
// and total stays 270 — only byte 5 changes 0x01 -> 0x80.
BOOST_AUTO_TEST_CASE(FailedDepositStatusIsEmptyString)
{
    auto dep = minimalDepositReceipt(/*status=*/1);  // FISCO non-zero = failed
    const auto enc = encodeReceiptForRoot(*dep, static_cast<uint8_t>(kDepositTxType));
    bcos::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x80, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected.insert(expected.end(), 256, 0x00);
    expected.insert(expected.end(), {0xc0, 0x05, 0x01});
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// Deposit with logs: the logs segment (incl. the vector-list wrapper) is compared as a
// whole against evmone's independent encoding; the tail 2 bytes are {nonce, version}.
// Total-length anchor (rev.2 red-team hardening, closing the wrapper-loss blind spot):
// the empty-logs version totals 270, where the logs item is 1 byte (0xc0) -> total =
// 269 + |rlp(logs)| (bloom encoding length is content-independent, always 259; nonce 7
// and 5 are both 1 byte).
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
    // evmone's vector<Log> list encoding (independent path) must appear whole — covers the list
    // wrapper bytes
    const auto logsBytes = evmone::rlp::encode(std::vector<evmone::state::Log>{evmoneLog});
    BOOST_CHECK(
        std::search(enc.begin(), enc.end(), logsBytes.begin(), logsBytes.end()) != enc.end());
    BOOST_REQUIRE_EQUAL(enc.size(), 269u + logsBytes.size());
    BOOST_CHECK_EQUAL(enc[enc.size() - 2], 0x07);  // nonce
    BOOST_CHECK_EQUAL(enc[enc.size() - 1], 0x01);  // version
}

// Multi-log shape the header-backfill rewrite made dangerous: the FIRST log's long-form list
// header (payload >= 56) inserts bytes and shifts the buffer, so the SECOND log's positions
// must be re-read after that insertion. First log carries 3 topics (ERC-20 Transfer shape:
// topics list payload 96 >= 56, also long form); second log has zero topics. The whole logs
// segment must equal evmone's independent vector<Log> encoding, and the size anchor pins that
// the first log's long-form header did not clobber the nonce/version tail.
BOOST_AUTO_TEST_CASE(DepositWithMultipleLogsEmbedsEncodedLogsAndNonceTail)
{
    constexpr auto kAddrA = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kAddrB = 0x00000000000000000000000000000000000000bb_address;
    evmone::state::Log logA{.addr = kAddrA,
        .data = evmc::bytes{0x68, 0x69},
        .topics = {0x01_bytes32, 0x02_bytes32, 0x03_bytes32}};
    evmone::state::Log logB{.addr = kAddrB, .data = evmc::bytes{}, .topics = {}};

    // Project both logs onto FISCO LogEntry (raw bytes, same mapping mapOpLogs uses).
    std::vector<bcos::protocol::LogEntry> logs;
    auto project = [](evmone::state::Log const& l) {
        bcos::h256s topics;
        for (auto const& t : l.topics)
            topics.emplace_back(t.bytes, sizeof(t.bytes));
        return bcos::protocol::LogEntry(
            bcos::bytes(l.addr.bytes, l.addr.bytes + sizeof(l.addr.bytes)), std::move(topics),
            bcos::bytes(l.data.begin(), l.data.end()));
    };
    logs.emplace_back(project(logA));
    logs.emplace_back(project(logB));
    auto dep = kOpTestReceiptFactory->createReceipt(
        bcos::u256(21000), std::string{}, logs, /*status=*/0, bcos::bytesConstRef{}, 1);
    dep->setCumulativeGasUsed("0x5208");
    evmone::state::Log const evmoneLogs[] = {logA, logB};
    const auto bloomF =
        evmone::state::compute_bloom_filter(std::span<const evmone::state::Log>{evmoneLogs, 2});
    bcos::bytes bloom(bloomF.bytes, bloomF.bytes + sizeof(bloomF.bytes));
    dep->setLogsBloom(bcos::ref(bloom));
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 7;
    meta.deposit_receipt_version = 1;
    dep->setOpStackMeta(std::move(meta));

    const auto enc = encodeReceiptForRoot(*dep, static_cast<uint8_t>(kDepositTxType));
    BOOST_CHECK_EQUAL(enc[0], 0x7e);
    const auto logsBytes = evmone::rlp::encode(std::vector<evmone::state::Log>{logA, logB});
    BOOST_CHECK(
        std::search(enc.begin(), enc.end(), logsBytes.begin(), logsBytes.end()) != enc.end());
    // 270 = empty-logs total (DepositGoldenBytes); logs item grows from 0xc0 (1B) to the full
    // multi-log encoding, and the nonce/version tail must survive the first log's long-form
    // header insertion.
    BOOST_REQUIRE_EQUAL(enc.size(), 269u + logsBytes.size());
    BOOST_CHECK_EQUAL(enc[enc.size() - 2], 0x07);  // nonce
    BOOST_CHECK_EQUAL(enc[enc.size() - 1], 0x01);  // version
}

// Normal tx: byte-for-byte equivalent to evmone rlp_encode (incl. the typed prefix) —
// rebuilt from a FISCO receipt and must match evmone's encoding of the same-shaped
// evmone receipt.
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

    BOOST_CHECK_EQUAL(enc[0], 0x02);  // eip1559 typed prefix
    BOOST_CHECK_EQUAL(enc, bcos::bytes(expected.begin(), expected.end()));
}

// legacy (no typed prefix) + access_list prefixes are also byte-identical to evmone
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
        BOOST_CHECK_EQUAL(enc, bcos::bytes(expected.begin(), expected.end()));
    };

    auto legacy = makeFisco(42000);
    compareToEvmone(legacy, evmone::state::Transaction::Type::legacy);
    auto accessList = makeFisco(42000);
    compareToEvmone(accessList, evmone::state::Transaction::Type::access_list);
}

// Deposit and normal txs must produce different leaves (prefix and tail fields both differ)
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

// End-to-end receiptsRoot equivalence (leaf-level proof): building
// the root from FISCO receipts via sealOpBlock must be bit-identical to building it from
// equivalent evmone receipts via evmone rlp_encode. Of sealOpBlock's three subtrees, only
// the receiptsRoot leaf encoding is reconstructed here (trie construction unchanged), so
// this case + NormalReceiptMatchesEvmoneEncoding together pin the receiptsRoot byte
// equivalence.
BOOST_AUTO_TEST_CASE(SealReceiptsRootMatchesEvmoneReferenceTrie)
{
    // Production-format FISCO receipt #0: decimal cumGas 21000.
    auto dep = minimalDepositReceipt(0, "21000");
    // FISCO receipt #1: eip1559 (status 0, cumGas 42000).
    auto normal = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    normal->setCumulativeGasUsed("42000");
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

// Independent single-leaf MPT derivation:
// key = keccak256(32 zero bytes) = 0x290dec...e563;
// leaf = rlp([hex-prefix(leaf,key), rlp(1)]) =
// 0xe3a120 || key || 0x01; root = keccak256(leaf).
BOOST_AUTO_TEST_CASE(OpStorageRootSingleSlotGolden)
{
    std::map<evmc::bytes32, evmc::bytes32> storage;
    evmc::bytes32 key{};
    evmc::bytes32 value{};
    value.bytes[sizeof(value.bytes) - 1] = 1;
    storage.emplace(key, value);

    const auto root = opStorageRoot(storage);
    const auto expected =
        0x821e2556a290c86405f8160a2d662042a431ba456b9db265c79bb837c04be5f0_bytes32;
    BOOST_CHECK_EQUAL(root, expected);
}

// A receipt whose logsBloom is not exactly 256 bytes must be a consensus rejection — a short
// bloom would silently produce a non-canonical receipts-root leaf and a wrong block bloom.
BOOST_AUTO_TEST_CASE(ShortLogsBloomIsConsensusReject)
{
    auto dep = minimalDepositReceipt();
    bcos::bytes shortBloom(128, 0xab);
    dep->setLogsBloom(bcos::ref(shortBloom));

    BOOST_CHECK_EXCEPTION((void)encodeReceiptForRoot(*dep, static_cast<uint8_t>(kDepositTxType)),
        bcos::evm::OpConsensusError, consensusWhatContains("logsBloom must be 256 bytes"));
}

// Isthmus+ deposit receipts always carry both tail fields. A lost optional must reject instead
// of silently committing a different leaf with a substituted zero.
BOOST_AUTO_TEST_CASE(DepositMissingNonceOrVersionIsConsensusReject)
{
    auto makeReceipt = [] {
        auto receipt = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
            std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
        receipt->setCumulativeGasUsed("21000");
        bcos::bytes bloom(256, 0x00);
        receipt->setLogsBloom(bcos::ref(bloom));
        return receipt;
    };

    auto missingMeta = makeReceipt();
    BOOST_CHECK_EXCEPTION(
        (void)encodeReceiptForRoot(*missingMeta, static_cast<uint8_t>(kDepositTxType)),
        bcos::evm::OpConsensusError,
        consensusWhatContains("missing deposit nonce/receipt version"));

    auto missingVersion = makeReceipt();
    bcos::protocol::OpStackReceiptMeta partialMeta;
    partialMeta.deposit_nonce = 5;
    missingVersion->setOpStackMeta(std::move(partialMeta));
    BOOST_CHECK_EXCEPTION(
        (void)encodeReceiptForRoot(*missingVersion, static_cast<uint8_t>(kDepositTxType)),
        bcos::evm::OpConsensusError,
        consensusWhatContains("missing deposit nonce/receipt version"));

    auto missingNonce = makeReceipt();
    bcos::protocol::OpStackReceiptMeta versionOnly;
    versionOnly.deposit_receipt_version = 1;
    missingNonce->setOpStackMeta(std::move(versionOnly));
    BOOST_CHECK_EXCEPTION(
        (void)encodeReceiptForRoot(*missingNonce, static_cast<uint8_t>(kDepositTxType)),
        bcos::evm::OpConsensusError,
        consensusWhatContains("missing deposit nonce/receipt version"));
}

// Engaged optional 0 is present, not absent: RLP integer 0 is the empty item 0x80 (not 0x00),
// version 1 stays 0x01. Same 266-byte payload / 270-byte leaf as DepositGoldenBytes, only the
// nonce tail byte changes 0x05 -> 0x80.
BOOST_AUTO_TEST_CASE(DepositExplicitZeroNonceEncodesEmptyRlpItem)
{
    auto dep = minimalDepositReceipt();
    BOOST_REQUIRE(dep->opStackMeta().has_value());
    auto meta = *dep->opStackMeta();
    meta.deposit_nonce = uint64_t{0};
    dep->setOpStackMeta(std::move(meta));

    const auto enc = encodeReceiptForRoot(*dep, static_cast<uint8_t>(kDepositTxType));
    bcos::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x01, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected.insert(expected.end(), 256, 0x00);
    expected.insert(expected.end(), {0xc0, 0x80, 0x01});
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// Measure the "wedprcrypto breaks libc++ typed catch binary-wide" claim on THIS target.
// If BOOST_CHECK_THROW binds OpConsensusError, the processOpBlock catch (const OpConsensusError&)
// ladder is executable in a binary that already links ledger + protocol-tars + bcos-crypto.
BOOST_AUTO_TEST_CASE(TypedCatchBindsOpConsensusErrorOnReceiptSuite)
{
    auto receipt = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    receipt->setCumulativeGasUsed("21000");
    bcos::bytes bloom(256, 0x00);
    receipt->setLogsBloom(bcos::ref(bloom));
    BOOST_CHECK_THROW((void)encodeReceiptForRoot(*receipt, static_cast<uint8_t>(kDepositTxType)),
        bcos::evm::OpConsensusError);
}

// Jovian blobGasUsed is Σ da_footprint over non-deposit receipts. Missing optional ≠ 0.
BOOST_AUTO_TEST_CASE(JovianMissingDaFootprintIsConsensusReject)
{
    auto makeNormal = [] {
        auto receipt = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
            std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
        receipt->setCumulativeGasUsed("21000");
        bcos::bytes bloom(256, 0x00);
        receipt->setLogsBloom(bcos::ref(bloom));
        return receipt;
    };

    bcos::evm::opstack::OpBlockResult missing;
    missing.receipts.push_back(makeNormal());
    missing.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    BOOST_CHECK_EXCEPTION(
        (void)bcos::evm::opstack::sealOpBlock(missing, bcos::evm::opstack::jovianConfig(), {}),
        bcos::evm::OpConsensusError, consensusWhatContains("missing da_footprint under Jovian"));

    auto zeroFootprint = makeNormal();
    bcos::protocol::OpStackReceiptMeta zeroMeta;
    zeroMeta.da_footprint = uint64_t{0};
    zeroFootprint->setOpStackMeta(std::move(zeroMeta));
    bcos::evm::opstack::OpBlockResult explicitZero;
    explicitZero.receipts.push_back(zeroFootprint);
    explicitZero.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    auto const zeroSeal =
        bcos::evm::opstack::sealOpBlock(explicitZero, bcos::evm::opstack::jovianConfig(), {});
    BOOST_REQUIRE(zeroSeal.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*zeroSeal.blobGasUsed, 0u);

    auto present = makeNormal();
    bcos::protocol::OpStackReceiptMeta presentMeta;
    presentMeta.da_footprint = uint64_t{7};
    present->setOpStackMeta(std::move(presentMeta));
    bcos::evm::opstack::OpBlockResult presentResult;
    presentResult.receipts.push_back(minimalDepositReceipt(0, "21000"));
    presentResult.receipts.push_back(present);
    presentResult.txTypes.push_back(static_cast<uint8_t>(kDepositTxType));
    presentResult.txTypes.push_back(
        static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    auto const presentSeal =
        bcos::evm::opstack::sealOpBlock(presentResult, bcos::evm::opstack::jovianConfig(), {});
    BOOST_REQUIRE(presentSeal.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*presentSeal.blobGasUsed, 7u);
}

BOOST_AUTO_TEST_CASE(JovianDaFootprintOverflowIsConsensusReject)
{
    auto makeWithFootprint = [](uint64_t fp) {
        auto receipt = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
            std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
        receipt->setCumulativeGasUsed("21000");
        bcos::bytes bloom(256, 0x00);
        receipt->setLogsBloom(bcos::ref(bloom));
        bcos::protocol::OpStackReceiptMeta meta;
        meta.da_footprint = fp;
        receipt->setOpStackMeta(std::move(meta));
        return receipt;
    };

    bcos::evm::opstack::OpBlockResult overflow;
    overflow.receipts.push_back(makeWithFootprint(std::numeric_limits<uint64_t>::max()));
    overflow.receipts.push_back(makeWithFootprint(1));
    overflow.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    overflow.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    BOOST_CHECK_EXCEPTION(
        (void)bcos::evm::opstack::sealOpBlock(overflow, bcos::evm::opstack::jovianConfig(), {}),
        bcos::evm::OpConsensusError, consensusWhatContains("DA footprint overflows uint64"));
}

// Round-14 F5: a receipts/txTypes length mismatch is an internal-invariant violation (a caller
// programming error), not a block-content rejection — it must surface as std::logic_error, never
// OpConsensusError (which maps to INVALID and would blame the block for the caller's bug).
BOOST_AUTO_TEST_CASE(ReceiptsTxTypesLengthMismatchIsLogicError)
{
    auto receipt = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    receipt->setCumulativeGasUsed("21000");
    bcos::bytes bloom(256, 0x00);
    receipt->setLogsBloom(bcos::ref(bloom));

    bcos::evm::opstack::OpBlockResult mismatch;
    mismatch.receipts.push_back(receipt);
    mismatch.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    mismatch.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    BOOST_CHECK_EXCEPTION(
        (void)bcos::evm::opstack::sealOpBlock(mismatch, bcos::evm::opstack::jovianConfig(), {}),
        std::logic_error, [](std::logic_error const& e) {
            return std::string(e.what()).find("receipts/txTypes length mismatch") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
