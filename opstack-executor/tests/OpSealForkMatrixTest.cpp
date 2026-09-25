// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSealForkMatrixTest — sealOpBlock's per-fork header-commitment matrix (M3b):
//   withdrawalsRoot: absent pre-Canyon; empty-trie root Canyon–Holocene; MessagePasser storage
//                    root Isthmus+.
//   requestsHash:    absent pre-Isthmus; sha256("") Isthmus+.
//   blobGasUsed:     absent pre-Ecotone; 0 Ecotone–Isthmus; DA footprint Jovian+.
// plus the deposit-receipt meta fork matrix the seal fails closed on (the leaf encoding itself
// is covered byte-for-byte in OpReceiptEncodeTest.cpp).

#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/test/opstack/OpTestReceiptFactory.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpCommitments.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <string>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmc::literals;

namespace
{
// EmptyWithdrawalsHash (Canyon–Holocene): keccak of the empty MPT — pinned as a literal so the
// test does not merely assert self-consistency with emptyRootHash().
const evmc::bytes32 kEmptyWithdrawalsHash =
    0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_bytes32;
// sha256("") — Isthmus+ requestsHash (EIP-7685 empty request list).
const evmc::bytes32 kEmptyRequestsHash =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

inline auto consensusWhatContains(std::string_view needle)
{
    return [needle](bcos::evm::OpConsensusError const& e) {
        return std::string(e.what()).find(std::string{needle}) != std::string::npos;
    };
}

/// Deposit receipt whose meta matches the fork (the shape runDeposit produces):
/// pre-Regolith neither field; Regolith–Delta nonce only; Canyon+ nonce + version 1.
bcos::protocol::TransactionReceipt::Ptr depositReceiptFor(const OpForkConfig& cfg)
{
    auto r = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/1);
    r->setCumulativeGasUsed("21000");
    bcos::bytes bloom(256, 0x00);
    r->setLogsBloom(bcos::ref(bloom));
    bcos::protocol::OpStackReceiptMeta meta;
    if (cfg.regolith_deposit_fixes)
        meta.deposit_nonce = 5;
    if (cfg.has_deposit_receipt_version)
        meta.deposit_receipt_version = 1;
    r->setOpStackMeta(std::move(meta));
    return r;
}

/// Normal eip1559 receipt; Jovian+ needs the da_footprint meta (else the seal fails closed).
bcos::protocol::TransactionReceipt::Ptr normalReceiptFor(
    const OpForkConfig& cfg, uint64_t daFootprint)
{
    auto r = kOpTestReceiptFactory->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/1);
    r->setCumulativeGasUsed("42000");
    bcos::bytes bloom(256, 0x00);
    r->setLogsBloom(bcos::ref(bloom));
    if (cfg.has_da_footprint)
    {
        bcos::protocol::OpStackReceiptMeta meta;
        meta.da_footprint = daFootprint;
        r->setOpStackMeta(std::move(meta));
    }
    return r;
}

OpBlockResult blockOf(const OpForkConfig& cfg)
{
    OpBlockResult result;
    result.receipts.push_back(depositReceiptFor(cfg));
    result.receipts.push_back(normalReceiptFor(cfg, /*daFootprint=*/7));
    result.txTypes.push_back(static_cast<uint8_t>(kDepositTxType));
    result.txTypes.push_back(static_cast<uint8_t>(evmone::state::Transaction::Type::eip1559));
    result.gasUsed = 63000;
    return result;
}

/// Non-empty MessagePasser slot map (single slot, value 1 — the OpStorageRootSingleSlotGolden
/// shape) so the Isthmus+ withdrawalsRoot is distinguishable from the empty-trie root.
std::map<evmc::bytes32, evmc::bytes32> messagePasserStorage()
{
    std::map<evmc::bytes32, evmc::bytes32> storage;
    evmc::bytes32 key{};
    evmc::bytes32 value{};
    value.bytes[sizeof(value.bytes) - 1] = 1;
    storage.emplace(key, value);
    return storage;
}

const std::vector<std::pair<const char*, const OpForkConfig*>>& allForks()
{
    static const std::vector<std::pair<const char*, const OpForkConfig*>> forks{
        {"Bedrock", &bedrockConfig()},   {"Regolith", &regolithConfig()},
        {"Canyon", &canyonConfig()},     {"Delta", &deltaConfig()},
        {"Ecotone", &ecotoneConfig()},   {"Fjord", &fjordConfig()},
        {"Granite", &graniteConfig()},   {"Holocene", &holoceneConfig()},
        {"Isthmus", &isthmusConfig()},   {"Jovian", &jovianConfig()},
        {"Karst", &karstConfig()},
    };
    return forks;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSealForkMatrixSuite)

BOOST_AUTO_TEST_CASE(WithdrawalsRootFollowsTheForkMatrix)
{
    for (const auto& [name, cfg] : allForks())
    {
        auto result = blockOf(*cfg);
        const auto seal = sealOpBlock(result, *cfg, messagePasserStorage());
        if (cfg->fork < OpFork::Canyon)
        {
            BOOST_CHECK_MESSAGE(!seal.withdrawalsRoot.has_value(),
                name << ": pre-Canyon seal must not carry withdrawalsRoot");
        }
        else if (cfg->fork < OpFork::Isthmus)
        {
            BOOST_REQUIRE_MESSAGE(seal.withdrawalsRoot.has_value(),
                name << ": Canyon+ seal must carry withdrawalsRoot");
            BOOST_CHECK_MESSAGE(*seal.withdrawalsRoot == kEmptyWithdrawalsHash,
                name << ": Canyon–Holocene withdrawalsRoot must be the empty-trie root");
        }
        else
        {
            BOOST_REQUIRE_MESSAGE(seal.withdrawalsRoot.has_value(),
                name << ": Isthmus+ seal must carry withdrawalsRoot");
            // Single-slot storage root golden (OpStorageRootSingleSlotGolden).
            BOOST_CHECK_MESSAGE(
                *seal.withdrawalsRoot ==
                    0x821e2556a290c86405f8160a2d662042a431ba456b9db265c79bb837c04be5f0_bytes32,
                name << ": Isthmus+ withdrawalsRoot must be the MessagePasser storage root");
            BOOST_CHECK_MESSAGE(*seal.withdrawalsRoot != kEmptyWithdrawalsHash,
                name << ": non-empty MessagePasser must not collapse to the empty-trie root");
        }
    }
}

// An Isthmus+ block whose MessagePasser was never touched still commits the empty-trie root —
// the value differs from Canyon–Holocene only in provenance, not bytes.
BOOST_AUTO_TEST_CASE(IsthmusEmptyMessagePasserYieldsEmptyTrieRoot)
{
    auto result = blockOf(isthmusConfig());
    const auto seal = sealOpBlock(result, isthmusConfig(), {});
    BOOST_REQUIRE(seal.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(*seal.withdrawalsRoot, kEmptyWithdrawalsHash);
}

BOOST_AUTO_TEST_CASE(RequestsHashOnlyFromIsthmus)
{
    for (const auto& [name, cfg] : allForks())
    {
        auto result = blockOf(*cfg);
        const auto seal = sealOpBlock(result, *cfg, {});
        if (cfg->fork >= OpFork::Isthmus)
        {
            BOOST_REQUIRE_MESSAGE(seal.requestsHash.has_value(),
                name << ": Isthmus+ seal must carry requestsHash");
            BOOST_CHECK_MESSAGE(*seal.requestsHash == kEmptyRequestsHash,
                name << ": Isthmus+ requestsHash must be sha256(\"\")");
        }
        else
        {
            BOOST_CHECK_MESSAGE(!seal.requestsHash.has_value(),
                name << ": pre-Isthmus seal must not carry requestsHash");
        }
    }
}

BOOST_AUTO_TEST_CASE(BlobGasUsedFollowsTheForkMatrix)
{
    for (const auto& [name, cfg] : allForks())
    {
        auto result = blockOf(*cfg);
        const auto seal = sealOpBlock(result, *cfg, {});
        if (cfg->fork < OpFork::Ecotone)
        {
            BOOST_CHECK_MESSAGE(!seal.blobGasUsed.has_value(),
                name << ": pre-Ecotone seal must not carry blobGasUsed");
        }
        else if (cfg->fork < OpFork::Jovian)
        {
            BOOST_REQUIRE_MESSAGE(
                seal.blobGasUsed.has_value(), name << ": Ecotone+ seal must carry blobGasUsed");
            BOOST_CHECK_MESSAGE(*seal.blobGasUsed == 0u,
                name << ": Ecotone–Isthmus blobGasUsed is fixed at 0");
        }
        else
        {
            // Jovian+: DA footprint — deposits skipped, the one normal receipt contributes 7.
            BOOST_REQUIRE_MESSAGE(
                seal.blobGasUsed.has_value(), name << ": Jovian+ seal must carry blobGasUsed");
            BOOST_CHECK_MESSAGE(
                *seal.blobGasUsed == 7u, name << ": Jovian+ blobGasUsed is the DA footprint sum");
        }
    }
}

// commitmentsOf must preserve the pre-Canyon absence end-to-end (the p2p replay path builds its
// announced side from the peer header — a computed-only field would be a spurious mismatch).
BOOST_AUTO_TEST_CASE(PreCanyonSealProjectsAbsentForkFields)
{
    auto result = blockOf(bedrockConfig());
    const auto seal = sealOpBlock(result, bedrockConfig(), {});
    const auto c = bcos::evm::engine::commitmentsOf(seal, bcos::h256{}, 63000, bcos::h256{});
    BOOST_CHECK(!c.withdrawalsRoot.has_value());
    BOOST_CHECK(!c.requestsHash.has_value());
    BOOST_CHECK(!c.blobGasUsed.has_value());
    BOOST_CHECK(c.gasUsed == bcos::u256{63000});
}

// ---- deposit receipt meta fork matrix (seal-side fail-closed guard) ----

BOOST_AUTO_TEST_CASE(CanyonDepositMissingVersionIsConsensusReject)
{
    auto result = blockOf(canyonConfig());
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 5;  // Regolith shape on a Canyon block
    result.receipts[0]->setOpStackMeta(std::move(meta));
    BOOST_CHECK_EXCEPTION((void)sealOpBlock(result, canyonConfig(), {}),
        bcos::evm::OpConsensusError,
        consensusWhatContains("Canyon+ deposit receipt missing deposit nonce/receipt version"));
}

BOOST_AUTO_TEST_CASE(PreCanyonDepositCarryingVersionIsConsensusReject)
{
    auto result = blockOf(regolithConfig());
    auto meta = result.receipts[0]->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    meta->deposit_receipt_version = 1;  // Canyon shape on a Regolith block
    result.receipts[0]->setOpStackMeta(std::move(*meta));
    BOOST_CHECK_EXCEPTION((void)sealOpBlock(result, regolithConfig(), {}),
        bcos::evm::OpConsensusError,
        consensusWhatContains("pre-Canyon deposit receipt carries deposit receipt version"));
}

BOOST_AUTO_TEST_CASE(PreRegolithDepositCarryingNonceIsConsensusReject)
{
    auto result = blockOf(bedrockConfig());
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 5;  // Regolith shape on a Bedrock block
    result.receipts[0]->setOpStackMeta(std::move(meta));
    BOOST_CHECK_EXCEPTION((void)sealOpBlock(result, bedrockConfig(), {}),
        bcos::evm::OpConsensusError,
        consensusWhatContains("pre-Regolith deposit receipt carries deposit nonce"));
}

// The receipts root must differ across the deposit-encoding boundary even though the receipts
// are otherwise identical: Canyon's 6-field leaf vs Regolith's 4-field leaf.
BOOST_AUTO_TEST_CASE(ReceiptsRootDiffersAcrossTheCanyonDepositEncodingBoundary)
{
    auto regolith = blockOf(regolithConfig());
    auto canyon = blockOf(canyonConfig());
    const auto regolithSeal = sealOpBlock(regolith, regolithConfig(), {});
    const auto canyonSeal = sealOpBlock(canyon, canyonConfig(), {});
    BOOST_CHECK(regolithSeal.receiptsRoot != canyonSeal.receiptsRoot);
}

BOOST_AUTO_TEST_SUITE_END()
