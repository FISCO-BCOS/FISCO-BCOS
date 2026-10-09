// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthReceiptGoldenTest — the externally anchored receipt-encoding goldens of the
// retired opstack-executor OpReceiptEncodeTest (deleted with the bcos-evm layer in
// the OP rewrite), re-pinned on the new-layer API: the receipt-leaf encoder is
// bcos::ledger::mpt::encodeReceiptLeaf (the single source sealOpEthBlock also
// uses) and the seal is sealOpEthBlock. The deposit leaf bytes are hand-derived
// RLP (formerly cross-checked against evmone's independent encoder); the
// receiptsRoot constant was computed by the reference implementation over those
// exact leaves and is pinned by value here.

#include <opstack-executor/OpEthBlockExecute.h>  // sealOpEthBlock / OP_DEPOSIT_TX_TYPE (via OpEthDeposit.h)
#include <opstack-executor/OpForkSpec.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>  // encodeReceiptLeaf
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#ifdef FISCO_WITH_CXX_MODULES
import bcos.utilities;
#else
#include <bcos-utilities/Common.h>
#endif

using namespace bcos::executor_v1::opstack;

namespace
{
bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    auto suite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
}

/// A deposit receipt projected onto the FISCO receipt: cumulative gas 21000, 256-zero bloom,
/// deposit_nonce/version in opStackMeta. `status` is the FISCO status (0 = success).
bcos::protocol::TransactionReceipt::Ptr minimalDepositReceipt(
    bcos::protocol::TransactionReceiptFactory const& factory, int32_t status = 0,
    std::string cumulativeGasUsed = "0x5208")
{
    auto r = factory.createReceipt(bcos::u256(21000), std::string{},
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
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthReceiptGoldenSuite)

// Hand-derived golden fixture byte-by-byte (the derivation is the anchor; the bytes were
// formerly cross-checked against evmone's independent receipt encoder). RLP list items:
// status success -> 0x01 (1B); cumGas 21000=0x5208 -> 0x82 52 08 (3B); bloom 256 zero
// bytes -> 0xb9 0x0100 + 00x256 (259B); logs [] -> 0xc0 (1B); nonce 5 -> 0x05 (1B);
// version 1 -> 0x01 (1B). Payload = 1+3+259+1+1+1 = 266 = 0x010a -> list header 0xf9 01 0a
// (3B). Prefix 0x7e. Total 1+3+266 = 270.
BOOST_AUTO_TEST_CASE(DepositGoldenBytes)
{
    auto const factory = makeReceiptFactory();
    const auto enc =
        bcos::ledger::mpt::encodeReceiptLeaf(*minimalDepositReceipt(*factory), OP_DEPOSIT_TX_TYPE);
    bcos::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x01, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected.insert(expected.end(), 256, 0x00);
    expected.insert(expected.end(), {0xc0, 0x05, 0x01});
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// Failed deposit: the status item is the empty string 0x80 (op-geth statusEncoding failure
// branch). 0x80 and the success 0x01 are both 1 byte, so the payload stays 266 = 0x010a and
// the total stays 270 — only byte 5 changes 0x01 -> 0x80.
BOOST_AUTO_TEST_CASE(FailedDepositStatusIsEmptyString)
{
    auto const factory = makeReceiptFactory();
    const auto enc = bcos::ledger::mpt::encodeReceiptLeaf(
        *minimalDepositReceipt(*factory, /*status=*/1), OP_DEPOSIT_TX_TYPE);
    bcos::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x80, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected.insert(expected.end(), 256, 0x00);
    expected.insert(expected.end(), {0xc0, 0x05, 0x01});
    BOOST_REQUIRE_EQUAL(enc.size(), 270u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// The eip1559 normal-tx leaf, hand-derived the same way (formerly the
// NormalReceiptMatchesEvmoneEncoding cross-check): status 0x01 (1B), cumGas
// 42000=0xa410 -> 0x82 a4 10 (3B), bloom (259B), logs 0xc0 (1B). Payload = 264 =
// 0x0108 -> 0xf9 01 08. Prefix 0x02. Total 268.
BOOST_AUTO_TEST_CASE(NormalReceiptGoldenBytes)
{
    auto const factory = makeReceiptFactory();
    auto r = factory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    r->setCumulativeGasUsed("0xa410");  // 42000
    bcos::bytes bloom(256, 0x00);
    r->setLogsBloom(bcos::ref(bloom));
    const auto enc = bcos::ledger::mpt::encodeReceiptLeaf(*r, /*eip1559*/ 0x02);
    bcos::bytes expected{0x02, 0xf9, 0x01, 0x08, 0x01, 0x82, 0xa4, 0x10, 0xb9, 0x01, 0x00};
    expected.insert(expected.end(), 256, 0x00);
    expected.push_back(0xc0);
    BOOST_REQUIRE_EQUAL(enc.size(), 268u);
    BOOST_CHECK_EQUAL(enc, expected);
}

// End-to-end receiptsRoot pin (the retired SealReceiptsRootMatchesEvmoneReferenceTrie,
// with the evmone reference trie replaced by the value it produced): a Canyon+ deposit
// receipt (decimal cumGas 21000, nonce 5, version 1) and an eip1559 receipt (decimal
// cumGas 42000), sealed under the Isthmus spec. The two leaves are pinned byte-for-byte
// by the cases above; the root below is what the reference trie construction committed
// over {rlp(0): depositLeaf, rlp(1): normalLeaf}.
BOOST_AUTO_TEST_CASE(SealReceiptsRootMatchesPinnedReferenceRoot)
{
    auto const factory = makeReceiptFactory();
    auto dep = minimalDepositReceipt(*factory, 0, "21000");
    auto normal = factory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{}, 1);
    normal->setCumulativeGasUsed("42000");
    bcos::bytes bloom(256, 0x00);
    normal->setLogsBloom(bcos::ref(bloom));

    std::vector<bcos::protocol::TransactionReceipt::Ptr> const receipts{dep, normal};
    std::vector<uint8_t> const txTypes{OP_DEPOSIT_TX_TYPE, 0x02};
    const auto seal = sealOpEthBlock(receipts, txTypes, OP_ISTHMUS_SPEC, {});
    // Pinned value: un-hashed keys rlp(0)=0x80 / rlp(1)=0x01 diverge at nibble 0, so the
    // root is a branch over two hashed leaf nodes (paths [0] / [1], hp bytes 0x30 / 0x31);
    // root = keccak256(rlp(branch)) — independently derived from the leaf bytes above.
    BOOST_CHECK_EQUAL(seal.receiptsRoot.hexPrefixed(),
        "0x732064fceb979003ba1f2c1da050242992812d5915b36b96aa1c44bebb46c40a");
}

// Independent single-leaf MPT derivation (formerly OpStorageRootSingleSlotGolden):
// key = keccak256(32 zero bytes) = 0x290dec...e563; leaf = rlp([hex-prefix(leaf,key),
// rlp(1)]) = 0xe3a120 || key || 0x01; root = keccak256(leaf).
BOOST_AUTO_TEST_CASE(OpStorageRootSingleSlotGolden)
{
    std::map<evmc::bytes32, evmc::bytes32> storage;
    evmc::bytes32 key{};
    evmc::bytes32 value{};
    value.bytes[sizeof(value.bytes) - 1] = 1;
    storage.emplace(key, value);

    const auto root = opEthStorageRoot(storage);
    BOOST_CHECK_EQUAL(
        root.hexPrefixed(), "0x821e2556a290c86405f8160a2d662042a431ba456b9db265c79bb837c04be5f0");
}

BOOST_AUTO_TEST_SUITE_END()
