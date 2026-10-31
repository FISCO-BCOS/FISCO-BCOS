#pragma once
// Golden assertion battery for the op-geth t8n corpus vectors — the dual-run migration (step
// 3.3) replacement for the retired old-leg comparison: instead of executing the legacy
// bcos-evm path as the baseline, the bcos-evm-free outcome is asserted directly against the
// vector's `_op_expected` block (op-geth v1.101702.2 expectations): the header commitments
// (gasUsed/receiptsRoot/logsBloom/withdrawalsRoot/requestsHash/blobGasUsed/stateRoot), the
// per-receipt fields (type/status/gasUsed/cumulativeGasUsed/logsCount/output + every `_op_*`
// receipt-meta key the expectation carries), and the postState account balances/nonces.
// Shared by OpEthExecutorDualRunTest (the t8n golden battery on the production
// OpEthExecutor + SchedulerSerialImpl path) and OpEthForkMatrixTest's synthetic vectors.

#include "SeedPreState.h"  // jsonU256/jsonU64/jsonBytes32

#include <opstack-executor/OpEthBlockExecute.h>  // OpEthBlockSeal / opEthClassifyTxType

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <ethereum-executor/EthereumState.h>  // eth::ethViewAccount
#include <json/json.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace opstack_test
{

/// Header commitments: the `_op_expected.header` block vs the executed outcome. @p goldenHeader
/// is the decoded golden RLP header, used only for txsRoot (not carried by `_op_expected`).
inline void checkGoldenHeader(Json::Value const& expected, opeth::OpEthBlockSeal const& seal,
    bcos::h256 const& stateRoot, bcos::h256 const& txRoot, uint64_t gasUsed,
    bcos::protocol::BlockHeader const& goldenHeader)
{
    BOOST_CHECK_EQUAL(stateRoot.hexPrefixed(), expected["stateRoot"].asString());
    BOOST_CHECK_EQUAL(seal.receiptsRoot.hexPrefixed(), expected["receiptsRoot"].asString());
    BOOST_CHECK_EQUAL(txRoot.hexPrefixed(), goldenHeader.txsRoot().hexPrefixed());
    BOOST_CHECK_EQUAL(bcos::u256(gasUsed), jsonU256(expected["gasUsed"].asString()));
    auto const expectedBloom = bcos::fromHex(expected["logsBloom"].asString());
    BOOST_REQUIRE_EQUAL(expectedBloom.size(), seal.logsBloom.size());
    BOOST_CHECK(std::equal(expectedBloom.begin(), expectedBloom.end(), seal.logsBloom.begin()));
    if (expected.isMember("withdrawalsRoot"))
    {
        BOOST_REQUIRE(seal.withdrawalsRoot.has_value());
        BOOST_CHECK_EQUAL(
            seal.withdrawalsRoot->hexPrefixed(), expected["withdrawalsRoot"].asString());
    }
    else
    {
        BOOST_CHECK(!seal.withdrawalsRoot.has_value());
    }
    if (expected.isMember("requestsHash"))
    {
        BOOST_REQUIRE(seal.requestsHash.has_value());
        BOOST_CHECK_EQUAL(seal.requestsHash->hexPrefixed(), expected["requestsHash"].asString());
    }
    else
    {
        BOOST_CHECK(!seal.requestsHash.has_value());
    }
    if (expected.isMember("blobGasUsed"))
    {
        BOOST_REQUIRE(seal.blobGasUsed.has_value());
        BOOST_CHECK_EQUAL(
            bcos::u256(*seal.blobGasUsed), jsonU256(expected["blobGasUsed"].asString()));
    }
    else
    {
        BOOST_CHECK(!seal.blobGasUsed.has_value());
    }
}

/// Every `_op_*` receipt-meta key the expectation carries must be present in the receipt's
/// opStackMeta with the golden value (u256-valued and uint64-valued keys share the two
/// helpers). Keys the expectation omits are not asserted on (the golden is the op-geth-
/// observable subset).
inline void checkGoldenReceiptMeta(
    Json::Value const& expected, bcos::protocol::TransactionReceipt const& receipt)
{
    auto const meta = receipt.opStackMeta();
    auto checkU64 = [&](char const* key, std::optional<uint64_t> const& field) {
        if (!expected.isMember(key))
            return;
        BOOST_REQUIRE_MESSAGE(meta.has_value(), "receipt misses opStackMeta for " << key);
        BOOST_REQUIRE_MESSAGE(field.has_value(), "receipt meta misses " << key);
        BOOST_CHECK_EQUAL(*field, jsonU64(expected[key].asString()));
    };
    auto checkU256 = [&](char const* key, std::optional<bcos::u256> const& field) {
        if (!expected.isMember(key))
            return;
        BOOST_REQUIRE_MESSAGE(meta.has_value(), "receipt misses opStackMeta for " << key);
        BOOST_REQUIRE_MESSAGE(field.has_value(), "receipt meta misses " << key);
        BOOST_CHECK_EQUAL(*field, jsonU256(expected[key].asString()));
    };
    checkU256("_op_l1_gas_price", meta ? meta->l1_gas_price : std::nullopt);
    checkU256("_op_l1_fee", meta ? meta->l1_fee : std::nullopt);
    checkU256("_op_l1_blob_base_fee", meta ? meta->l1_blob_base_fee : std::nullopt);
    checkU64("_op_l1_base_fee_scalar", meta ? meta->l1_base_fee_scalar : std::nullopt);
    checkU64("_op_l1_blob_base_fee_scalar", meta ? meta->l1_blob_base_fee_scalar : std::nullopt);
    checkU64("_op_operator_fee_scalar", meta ? meta->operator_fee_scalar : std::nullopt);
    checkU64("_op_operator_fee_constant", meta ? meta->operator_fee_constant : std::nullopt);
    checkU256("_op_operator_fee", meta ? meta->operator_fee : std::nullopt);
    checkU64("_op_da_footprint_gas_scalar", meta ? meta->da_footprint_gas_scalar : std::nullopt);
    checkU64("_op_da_footprint", meta ? meta->da_footprint : std::nullopt);
    checkU64("_op_deposit_nonce", meta ? meta->deposit_nonce : std::nullopt);
    checkU64("_op_deposit_receipt_version", meta ? meta->deposit_receipt_version : std::nullopt);
    checkU64("_op_l1_gas_used", meta ? meta->l1_gas_used : std::nullopt);
}

/// Per-receipt golden battery: type/status/gasUsed/cumulativeGasUsed/logsCount/output plus the
/// `_op_*` meta keys. Golden `status` follows the Ethereum convention (0x1 = success); the
/// FISCO receipt reports 0 = success, so the two are cross-mapped.
inline void checkGoldenReceipts(Json::Value const& expectedReceipts,
    std::vector<bcos::protocol::TransactionReceipt::Ptr> const& receipts,
    std::vector<bcos::bytes> const& rawTxBytes)
{
    BOOST_REQUIRE_EQUAL(receipts.size(), rawTxBytes.size());
    BOOST_REQUIRE_EQUAL(receipts.size(), expectedReceipts.size());
    for (std::size_t i = 0; i < receipts.size(); ++i)
    {
        BOOST_TEST_CONTEXT("receipt " << i)
        {
            auto const& expected = expectedReceipts[static_cast<Json::ArrayIndex>(i)];
            auto const& receipt = *receipts[i];
            auto const expectedType = static_cast<uint8_t>(jsonU64(expected["type"].asString()));
            BOOST_REQUIRE(!rawTxBytes[i].empty());
            BOOST_CHECK_EQUAL(opeth::opEthClassifyTxType(rawTxBytes[i][0]), expectedType);
            // Ethereum 0x1 (success) <-> FISCO status 0.
            BOOST_CHECK_EQUAL(receipt.status() == 0, jsonU64(expected["status"].asString()) == 1);
            BOOST_CHECK_EQUAL(receipt.gasUsed(), jsonU256(expected["gasUsed"].asString()));
            BOOST_CHECK_EQUAL(bcos::u256(std::string{receipt.cumulativeGasUsed()}),
                jsonU256(expected["cumulativeGasUsed"].asString()));
            BOOST_CHECK_EQUAL(
                receipt.logEntries().size(), jsonU64(expected["logsCount"].asString()));
            BOOST_CHECK_EQUAL("0x" + bcos::toHex(receipt.output()), expected["output"].asString());
            checkGoldenReceiptMeta(expected, receipt);
        }
    }
}

/// postState balance/nonce spot-check (the stateRoot already pins the full state; this makes a
/// divergence locally diagnosable without a trie walk).
template <class View>
void checkGoldenPostState(View& view, Json::Value const& postState)
{
    for (auto const& addrKey : postState.getMemberNames())
    {
        BOOST_TEST_CONTEXT("postState account " << addrKey)
        {
            auto const& expected = postState[addrKey];
            auto account = bcos::executor_v1::eth::ethViewAccount(view, jsonAddress(addrKey));
            if (expected.isMember("balance"))
                BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()),
                    jsonU256(expected["balance"].asString()));
            if (expected.isMember("nonce"))
                BOOST_CHECK_EQUAL(bcos::task::syncWait(account.nonce()).value_or("0"),
                    std::to_string(jsonU64(expected["nonce"].asString())));
        }
    }
}

}  // namespace opstack_test
