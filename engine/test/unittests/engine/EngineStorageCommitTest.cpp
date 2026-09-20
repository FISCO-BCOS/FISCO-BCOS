// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EngineStorageCommitSuite — the engine-side receipts-commit helper keys the deposit
// leaf shape on the block's fork (the same key the executor seal, encodeReceiptForRoot,
// uses), never on the receipt's own metadata, and fails closed on meta/fork
// disagreement instead of silently encoding a leaf the executor would never produce.

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <engine/bcos-engine/EngineStorageCommit.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
struct RawOnlyTx
{
    bcos::bytes raw;
};

bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory()
{
    static const auto factory = std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr));
    return factory;
}

bcos::protocol::TransactionReceipt::Ptr depositReceipt(
    std::optional<std::uint64_t> nonce, std::optional<std::uint64_t> version)
{
    auto r = receiptFactory()->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, 0, bcos::bytesConstRef{}, 1);
    r->setCumulativeGasUsed(std::string{"0x5208"});
    if (nonce.has_value() || version.has_value())
    {
        bcos::protocol::OpStackReceiptMeta meta;
        meta.deposit_nonce = nonce;
        meta.deposit_receipt_version = version;
        r->setOpStackMeta(std::move(meta));
    }
    return r;
}

// The root the helper commits iff it selected `withNonceVersion` for the leaf: same
// one-leaf indexed trie, with the leaf encoded by the shared encoder under an explicit
// shape flag. The suite asserts WHICH flag the helper passed, not the encoder itself.
bcos::h256 rootOverLeaf(
    const bcos::protocol::TransactionReceipt& r, std::uint8_t type, bool withNonceVersion)
{
    auto leaf = bcos::ledger::mpt::encodeReceiptLeaf(r, type, withNonceVersion);
    std::vector<bcos::bytesConstRef> refs;
    refs.emplace_back(leaf.data(), leaf.size());
    return bcos::ledger::mpt::calculateReceiptsRoot(refs);
}

constexpr std::uint8_t c_depositTxType = 0x7e;

// BOOST_CHECK_THROW pins only the type; the errinfo_comment substring pins WHICH
// guard fired (same discipline as bcos-tool's ExceptionCheck.h).
bool engineErrorContains(bcos::engine::OpExecutionInternalError const& e, std::string_view needle)
{
    auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e);
    return msg != nullptr && msg->find(needle) != std::string::npos;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineStorageCommitSuite)

BOOST_AUTO_TEST_CASE(canyonForkKeysDepositLeafWithNonceVersion)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{depositReceipt(5, 1)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    const auto out = bcos::engine::engine_common::buildHeaderCommitments(
        payload, receipts, types, bcos::engine::OpForkId::Canyon);
    BOOST_CHECK_EQUAL(out.receiptsRoot, rootOverLeaf(*receipts[0], c_depositTxType, true));
    BOOST_CHECK(out.receiptsRoot != rootOverLeaf(*receipts[0], c_depositTxType, false));
}

BOOST_AUTO_TEST_CASE(regolithForkKeysDepositLeafWithoutNonceVersion)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{depositReceipt(5, std::nullopt)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    const auto out = bcos::engine::engine_common::buildHeaderCommitments(
        payload, receipts, types, bcos::engine::OpForkId::Regolith);
    // The wrong selector (withNonceVersion=true) would not merely mismatch — the shared
    // encoder refuses a version-less meta in the Canyon shape and throws inside the
    // helper, so equality against the pre-Canyon leaf pins the selection completely.
    BOOST_CHECK_EQUAL(out.receiptsRoot, rootOverLeaf(*receipts[0], c_depositTxType, false));
}

BOOST_AUTO_TEST_CASE(laneWithoutForkContextRejectsDepositReceipt)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{depositReceipt(5, 1)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    BOOST_CHECK_EXCEPTION((void)bcos::engine::engine_common::buildHeaderCommitments(
                              payload, receipts, types, std::nullopt),
        bcos::engine::OpExecutionInternalError,
        [](auto const& e) { return engineErrorContains(e, "never executes deposits"); });
}

BOOST_AUTO_TEST_CASE(canyonRejectsMissingVersionMeta)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{depositReceipt(5, std::nullopt)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    BOOST_CHECK_EXCEPTION((void)bcos::engine::engine_common::buildHeaderCommitments(
                              payload, receipts, types, bcos::engine::OpForkId::Canyon),
        bcos::engine::OpExecutionInternalError,
        [](auto const& e) { return engineErrorContains(e, "does not match the block's fork"); });
}

BOOST_AUTO_TEST_CASE(regolithRejectsVersionMeta)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{depositReceipt(5, 1)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    BOOST_CHECK_EXCEPTION((void)bcos::engine::engine_common::buildHeaderCommitments(
                              payload, receipts, types, bcos::engine::OpForkId::Regolith),
        bcos::engine::OpExecutionInternalError,
        [](auto const& e) { return engineErrorContains(e, "does not match the block's fork"); });
}

BOOST_AUTO_TEST_CASE(canyonRejectsMissingNonce)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{depositReceipt(std::nullopt, 1)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    BOOST_CHECK_EXCEPTION((void)bcos::engine::engine_common::buildHeaderCommitments(
                              payload, receipts, types, bcos::engine::OpForkId::Canyon),
        bcos::engine::OpExecutionInternalError,
        [](auto const& e) { return engineErrorContains(e, "without a deposit nonce"); });
}

BOOST_AUTO_TEST_CASE(canyonRejectsMissingMeta)
{
    std::vector<RawOnlyTx> payload{{.raw = {0x7e, 0x01}}};
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts{
        depositReceipt(std::nullopt, std::nullopt)};
    const std::vector<std::uint8_t> types{c_depositTxType};

    BOOST_CHECK_EXCEPTION((void)bcos::engine::engine_common::buildHeaderCommitments(
                              payload, receipts, types, bcos::engine::OpForkId::Canyon),
        bcos::engine::OpExecutionInternalError,
        [](auto const& e) { return engineErrorContains(e, "does not match the block's fork"); });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
