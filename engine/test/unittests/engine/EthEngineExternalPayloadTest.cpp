/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file EthEngineExternalPayloadTest.cpp
 * @brief EL-mode external newPayload lane: the L1 payload adapter (executionPayloadToEthBlock,
 *        validateExecutionPayloadL1, calculateRequestsHash) and the EthEngineService routing
 *        to the injected IExternalPayloadVerifier seam.
 */

#include "engine/bcos-engine/EthEngineService.h"
#include "engine/test/unittests/engine/EthServiceStubs.h"

#include <bcos-crypto/hash/Sha256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-task/Wait.h>
#include <boost/algorithm/hex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <limits>
#include <optional>
#include <string>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::engine::eth_test;

namespace
{
constexpr protocol::BlockNumber c_headNumber = 5;
constexpr std::uint64_t c_timestampMs = c_defaultPayloadTimestamp;  // 1700000000 s, whole
const h256 c_headHash{"9999999999999999999999999999999999999999999999999999999999999999"};

using Service = EthEngineService<bcos::txpool::MemPoolImpl, RealGlobalStateStorage, StubExecutor,
    StubScheduler>;

class MockExternalPayloadVerifier
    : public engine_common::IExternalPayloadVerifier<RealGlobalStateStorage>
{
public:
    MockExternalPayloadVerifier()
    {
        // A default build the header finalizer accepts: the deterministic roots must be
        // non-zero (validateHeader rejects all-zero roots as "missing").
        buildResult.ok = true;
        buildResult.stateRoot =
            h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        buildResult.computation.txsRoot = ledger::mpt::emptyRootHash();
        buildResult.computation.receiptsRoot = ledger::mpt::emptyRootHash();
    }

    task::Task<engine_common::ExternalPayloadResult> verifyAndCommit(
        RealGlobalStateStorage&, engine_common::ExternalPayloadBlock const& block) override
    {
        ++calls;
        lastBlock = block;
        co_return result;
    }
    task::Task<engine_common::ExternalRollbackResult> rollbackToCommitted(
        RealGlobalStateStorage&, bcos::protocol::BlockNumber targetNumber) override
    {
        ++rollbackCalls;
        lastRollbackTarget = targetNumber;
        co_return rollbackResult;
    }
    task::Task<engine_common::ExternalL1Context> deriveL1Context(
        bcos::protocol::EthBlockHeaderData const& parentHeader,
        int64_t timestampSeconds) override
    {
        ++deriveCalls;
        lastDeriveParent = parentHeader;
        lastDeriveTimestamp = timestampSeconds;
        co_return l1Context;
    }
    task::Task<engine_common::ExternalBuildResult> buildL1Block(
        RealGlobalStateStorage::ViewType&, engine_common::ExternalBuildBlock const& block) override
    {
        ++buildCalls;
        lastBuild = block;
        co_return buildResult;
    }

    unsigned calls = 0;
    std::optional<engine_common::ExternalPayloadBlock> lastBlock;
    engine_common::ExternalPayloadResult result{
        .outcome = engine_common::ExternalPayloadOutcome::Valid, .error = {}};
    unsigned rollbackCalls = 0;
    std::optional<bcos::protocol::BlockNumber> lastRollbackTarget;
    engine_common::ExternalRollbackResult rollbackResult{.rolledBack = true, .error = {}};
    unsigned deriveCalls = 0;
    std::optional<bcos::protocol::EthBlockHeaderData> lastDeriveParent;
    int64_t lastDeriveTimestamp = 0;
    engine_common::ExternalL1Context l1Context{.baseFee = 7,
        .excessBlobGas = 0,
        .forkVersion = bcos::protocol::EthBlockVersion::CANCUN};
    unsigned buildCalls = 0;
    std::optional<engine_common::ExternalBuildBlock> lastBuild;
    engine_common::ExternalBuildResult buildResult;
};

/// Independent re-derivation of the documented payload -> EthBlockHeaderData mapping (the
/// adapter's contract): PoS constants, seconds timestamp, locally computed trie roots, and
/// EIP-7685 requestsHash spelled out inline so the test does not lean on the implementation
/// under test.
protocol::EthBlockHeaderData expectedEthHeader(NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    protocol::EthBlockHeaderData header;
    header.logsBloom = payload.logsBloom;
    header.parentInfo = protocol::ParentInfo{
        .blockNumber = payload.blockNumber - 1, .blockHash = payload.parentHash};
    header.uncleHash = protocol::c_emptyOmmersHash;
    header.stateRoot = payload.stateRoot;
    std::vector<bcos::bytesConstRef> txRefs;
    for (auto const& tx : payload.transactions)
    {
        txRefs.push_back(bcos::ref(tx.raw));
    }
    header.txsRoot = ledger::mpt::calculateTransactionsRoot(txRefs);
    header.receiptsRoot = payload.receiptsRoot;
    header.difficulty = u256(0);
    header.gasLimit = payload.gasLimit;
    header.gasUsed = payload.gasUsed;
    header.prevRandao = payload.prevRandao;
    header.extraData = payload.extraData;
    header.coinbase = payload.feeRecipient;
    header.nonce = engine_common::c_posNonce;
    header.number = payload.blockNumber;
    header.timestamp = static_cast<int64_t>(payload.timestamp / 1000);
    header.baseFee = payload.baseFeePerGas;
    header.blobGasUsed = payload.blobGasUsed;
    header.excessBlobGas = payload.excessBlobGas;
    header.parentBeaconRoot = request.parentBeaconBlockRoot;
    if (request.executionRequests.has_value())
    {
        bcos::bytes digests;
        for (auto const& entry : *request.executionRequests)
        {
            auto const digest = crypto::sha256Hash(bcos::ref(entry));
            digests.insert(digests.end(), digest.begin(), digest.end());
        }
        header.requestsHash = crypto::sha256Hash(bcos::ref(digests));
    }
    if (payload.withdrawals.has_value())
    {
        std::vector<bcos::bytes> encoded;
        for (auto const& w : *payload.withdrawals)
        {
            protocol::EthWithdrawalData data;
            data.index = static_cast<std::uint64_t>(w.index);
            data.validatorIndex = static_cast<std::uint64_t>(w.validatorIndex);
            data.address = w.address;
            data.amount = static_cast<std::uint64_t>(w.amount);
            bcos::bytes item;
            codec::rlp::encode(item, data);
            encoded.push_back(std::move(item));
        }
        std::vector<bcos::bytesConstRef> refs;
        for (auto const& item : encoded)
        {
            refs.push_back(bcos::ref(item));
        }
        header.withdrawalsHash = ledger::mpt::calculateWithdrawalsRoot(refs);
    }
    return header;
}

/// A Shanghai (V2) external payload whose blockHash commits to the expected header.
NewPayloadRequest makeExternalRequestV2(h256 const& parentHash, protocol::BlockNumber number,
    std::vector<bcos::bytes> rawTxs = {}, std::optional<std::vector<WithdrawalV1>> withdrawals =
                                          std::vector<WithdrawalV1>{})
{
    NewPayloadRequest request;
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.feeRecipient = Address("1234567890abcdef1234567890abcdef12345678");
    payload.stateRoot = h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    payload.receiptsRoot =
        h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    payload.prevRandao = h256("1111111111111111111111111111111111111111111111111111111111111111");
    payload.gasLimit = 30000000;
    payload.gasUsed = 21000 * rawTxs.size();
    payload.baseFeePerGas = 7;
    payload.timestamp = c_timestampMs;
    payload.blockNumber = number;
    payload.extraData = bytes{0xde, 0xad};
    for (auto& raw : rawTxs)
    {
        payload.transactions.push_back(EngineTransaction{.raw = std::move(raw), .decoded = nullptr});
    }
    payload.withdrawals = std::move(withdrawals);
    payload.blockHash = protocol::ethHeaderHash(expectedEthHeader(request));
    return request;
}

NewPayloadRequest makeExternalRequestV4(h256 const& parentHash, protocol::BlockNumber number,
    std::vector<bcos::bytes> executionRequests)
{
    auto request = makeExternalRequestV2(parentHash, number);
    auto& payload = request.executionPayload;
    payload.blobGasUsed = u256(0);
    payload.excessBlobGas = u256(0);
    request.parentBeaconBlockRoot =
        h256("5555555555555555555555555555555555555555555555555555555555555555");
    request.executionRequests = std::move(executionRequests);
    payload.blockHash = protocol::ethHeaderHash(expectedEthHeader(request));
    return request;
}

struct ExternalPayloadFixture
{
    RealGlobalStateStorageFixture storageFixture;
    bcos::txpool::MemPoolImpl memPool;
    StubExecutor executor;
    StubScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    std::shared_ptr<MockExternalPayloadVerifier> mockVerifier =
        std::make_shared<MockExternalPayloadVerifier>();
    // The [engine_rpc] EL wiring: the service shares this coordination object with the
    // (here absent) devp2p sync loop, so tests can observe the backfill targets and the
    // CL-driven latch the SYNCING answers / forkchoiceUpdated calls post to it.
    std::shared_ptr<engine_common::ClSyncCoordination> clSync =
        std::make_shared<engine_common::ClSyncCoordination>();
    Service service;

    ExternalPayloadFixture()
      : service(memPool, storageFixture.storage, executor, scheduler, blockFactory,
            /*ledger=*/nullptr, engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
            /*ledgerConfigState=*/nullptr, mockVerifier, clSync)
    {}

    /// Write just the number-keyed header row (the parent-header source for the EL
    /// build lane's L1 context derivation).
    void seedHeaderRow(protocol::BlockNumber number)
    {
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(number);
        bcos::bytes buffer;
        header->encode(buffer);
        storage::Entry headerEntry;
        headerEntry.set(std::move(buffer));
        task::syncWait(storage2::writeOne(storageFixture.backendStorage,
            executor_v1::StateKey{
                ledger::SYS_NUMBER_2_BLOCK_HEADER, boost::lexical_cast<std::string>(number)},
            std::move(headerEntry)));
    }

    /// The committed-head rows the external lane reads: current number, the canonical
    /// hash mapping at that number, and the header row the parent conversion decodes.
    void seedHead(h256 const& headHash = c_headHash, protocol::BlockNumber head = c_headNumber)
    {
        auto& backend = storageFixture.backendStorage;
        storage::Entry numberEntry;
        numberEntry.set(boost::lexical_cast<std::string>(head));
        task::syncWait(storage2::writeOne(backend,
            executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER},
            std::move(numberEntry)));
        writeHashToNumber(backend, headHash, head);
        writeNumberToHash(backend, head, headHash);
        seedHeaderRow(head);
    }
};

WithdrawalV1 makeWithdrawal(uint64_t index, uint64_t validator, uint64_t amount)
{
    return WithdrawalV1{
        .index = index, .validatorIndex = validator, .amount = amount,
        .address = Address("5656565656565656565656565656565656565656")};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthEngineExternalPayloadTest)

BOOST_AUTO_TEST_CASE(requestsHashMatchesEip7685)
{
    // Empty list: sha256("") — the framework's single-sourced constant.
    BOOST_CHECK_EQUAL("0x" + engine::detail::calculateRequestsHash({}).hex(),
        std::string{c_emptyRequestsHashHex});
    // One entry: sha256 over the concatenated per-entry sha256 digests.
    bytes const entry{0x01, 0x02, 0x03};
    auto const inner = crypto::sha256Hash(bcos::ref(entry));
    auto const expected = crypto::sha256Hash(bytesConstRef(inner.data(), inner.size()));
    BOOST_CHECK_EQUAL(engine::detail::calculateRequestsHash({entry}), expected);
    // Order matters.
    bytes const other{0x04, 0x05};
    BOOST_CHECK_NE(engine::detail::calculateRequestsHash({entry, other}),
        engine::detail::calculateRequestsHash({other, entry}));
}

BOOST_AUTO_TEST_CASE(validateExecutionPayloadL1Dialect)
{
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    auto& payload = request.executionPayload;

    // The happy-shape V2 payload passes.
    BOOST_CHECK(!engine::detail::validateExecutionPayloadL1(payload, 2).has_value());

    // Blob transactions (0x03) are L1 citizens — the OP gate rejects them, L1 must not.
    payload.transactions.push_back(EngineTransaction{.raw = bytes{0x03, 0xc0}, .decoded = nullptr});
    BOOST_CHECK(!engine::detail::validateExecutionPayloadL1(payload, 2).has_value());
    payload.transactions.clear();

    // Deposit envelopes (0x7e) are the OP-Stack extension and never valid on L1.
    payload.transactions.push_back(EngineTransaction{.raw = bytes{0x7e, 0xc0}, .decoded = nullptr});
    auto error = engine::detail::validateExecutionPayloadL1(payload, 2);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_NE(error->find("deposit"), std::string::npos);
    payload.transactions.clear();

    // Unknown type bytes are rejected on both lanes.
    payload.transactions.push_back(EngineTransaction{.raw = bytes{0x05, 0xc0}, .decoded = nullptr});
    BOOST_CHECK(engine::detail::validateExecutionPayloadL1(payload, 2).has_value());
    payload.transactions.clear();

    // Withdrawals versioning: V1 forbids them, V2+ requires them (non-empty allowed).
    BOOST_CHECK(engine::detail::validateExecutionPayloadL1(payload, 1).has_value());
    payload.withdrawals = std::nullopt;
    BOOST_CHECK(engine::detail::validateExecutionPayloadL1(payload, 2).has_value());
    payload.withdrawals = std::vector<WithdrawalV1>{};
    BOOST_CHECK(!engine::detail::validateExecutionPayloadL1(payload, 2).has_value());

    // Blob gas fields: absent at V1/V2, required at V3+.
    payload.blobGasUsed = u256(0);
    BOOST_CHECK(engine::detail::validateExecutionPayloadL1(payload, 2).has_value());
    payload.blobGasUsed = std::nullopt;
    BOOST_CHECK(engine::detail::validateExecutionPayloadL1(payload, 3).has_value());
    payload.blobGasUsed = u256(0);
    payload.excessBlobGas = u256(0);
    BOOST_CHECK(!engine::detail::validateExecutionPayloadL1(payload, 3).has_value());

    // withdrawalsRoot is the OP Isthmus wire extension; an L1 payload must not carry it.
    payload.withdrawalsRoot = h256{};
    BOOST_CHECK(engine::detail::validateExecutionPayloadL1(payload, 4).has_value());
}

BOOST_AUTO_TEST_CASE(adapterRoundtripAndRejection)
{
    auto request = makeExternalRequestV2(
        c_headHash, c_headNumber + 1, /*rawTxs=*/{bytes{0x02, 0xc0}, bytes{0xf8, 0x6a}},
        std::vector<WithdrawalV1>{makeWithdrawal(7, 42, 32000000000ULL)});

    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);
    BOOST_CHECK(external->ethHeader == expectedEthHeader(request));
    BOOST_REQUIRE_EQUAL(external->rawTransactions.size(), 2);
    BOOST_CHECK(external->rawTransactions[0] == request.executionPayload.transactions[0].raw);
    BOOST_REQUIRE(external->rawWithdrawals.has_value());
    BOOST_CHECK_EQUAL(external->rawWithdrawals->size(), 1);

    // A tampered blockHash fails the reconstruction check.
    auto tampered = request;
    tampered.executionPayload.blockHash =
        h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    BOOST_CHECK(std::holds_alternative<std::string>(
        engine::detail::executionPayloadToEthBlock(tampered)));

    // A sub-second timestamp cannot be an L1 header timestamp.
    auto subSecond = request;
    subSecond.executionPayload.timestamp = c_timestampMs + 1;
    BOOST_CHECK(
        std::holds_alternative<std::string>(engine::detail::executionPayloadToEthBlock(subSecond)));

    // A withdrawal field above uint64 range has no RLP-domain representation.
    auto overflowing = request;
    (*overflowing.executionPayload.withdrawals)[0].amount =
        u256(1) << 64;
    BOOST_CHECK(
        std::holds_alternative<std::string>(engine::detail::executionPayloadToEthBlock(overflowing)));

    // V4: executionRequests commit to the header requestsHash.
    auto requestV4 = makeExternalRequestV4(
        c_headHash, c_headNumber + 1, {bytes{0x00, 0x11, 0x22}, bytes{0x01, 0x33}});
    auto convertedV4 = engine::detail::executionPayloadToEthBlock(requestV4);
    auto* externalV4 = std::get_if<engine_common::ExternalPayloadBlock>(&convertedV4);
    BOOST_REQUIRE(externalV4 != nullptr);
    BOOST_REQUIRE(externalV4->ethHeader.requestsHash.has_value());
    BOOST_CHECK_EQUAL(externalV4->ethHeader.requestsHash->hex(),
        expectedEthHeader(requestV4).requestsHash->hex());
    BOOST_CHECK(externalV4->ethHeader == expectedEthHeader(requestV4));
}

BOOST_FIXTURE_TEST_CASE(externalPayloadValidPath, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1,
        /*rawTxs=*/{bytes{0x02, 0xc0}},
        std::vector<WithdrawalV1>{makeWithdrawal(0, 1, 2)});
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*status.latestValidHash, request.executionPayload.blockHash);

    BOOST_REQUIRE_EQUAL(mockVerifier->calls, 1);
    auto const& block = *mockVerifier->lastBlock;
    BOOST_CHECK_EQUAL(block.ethHeader.number, c_headNumber + 1);
    // The parent header was read from the seeded ledger row.
    BOOST_CHECK_EQUAL(block.parentHeader.number, c_headNumber);
    BOOST_REQUIRE_EQUAL(block.rawTransactions.size(), 1);
    BOOST_CHECK(block.rawTransactions[0] == request.executionPayload.transactions[0].raw);
    BOOST_REQUIRE(block.rawWithdrawals.has_value());
    BOOST_CHECK_EQUAL(block.rawWithdrawals->size(), 1);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadUnknownParentSyncing, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(
        h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"), c_headNumber + 1);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadFutureHeightSyncing, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 3);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadStaleHeightSyncing, ExternalPayloadFixture)
{
    seedHead();
    // At-or-behind the head but never committed (no HASH_2_NUMBER row): a side fork
    // this node holds no state for.
    auto request = makeExternalRequestV2(c_headHash, c_headNumber);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadDuplicateIsIdempotentValid, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    // The hash is already committed at this block's number (HASH_2_NUMBER row).
    writeHashToNumber(
        storageFixture.backendStorage, request.executionPayload.blockHash, c_headNumber + 1);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*status.latestValidHash, request.executionPayload.blockHash);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadCommittedHashAtOtherNumberInvalid, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    writeHashToNumber(
        storageFixture.backendStorage, request.executionPayload.blockHash, c_headNumber + 2);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Invalid);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadTamperedBlockHashRejected, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    request.executionPayload.blockHash =
        h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::InvalidBlockHash);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadVerifierInvalidMapsToInvalid, ExternalPayloadFixture)
{
    seedHead();
    mockVerifier->result = engine_common::ExternalPayloadResult{
        .outcome = engine_common::ExternalPayloadOutcome::Invalid, .error = "stateRoot mismatch"};
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Invalid);
    // The committed head (the parent) is the latest valid hash for forkchoice recovery.
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*status.latestValidHash, c_headHash);
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK_EQUAL(*status.validationError, "stateRoot mismatch");
    BOOST_CHECK_EQUAL(mockVerifier->calls, 1);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadVerifierStaleMapsToSyncing, ExternalPayloadFixture)
{
    seedHead();
    mockVerifier->result = engine_common::ExternalPayloadResult{
        .outcome = engine_common::ExternalPayloadOutcome::StaleOrOutOfOrder, .error = {}};
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 1);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadDepositTransactionInvalid, ExternalPayloadFixture)
{
    seedHead();
    auto request =
        makeExternalRequestV2(c_headHash, c_headNumber + 1, /*rawTxs=*/{bytes{0x7e, 0xc0}});
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Invalid);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadBlobTransactionReachesVerifier, ExternalPayloadFixture)
{
    seedHead();
    // L1 admits blob transactions: the shape gate must not reject 0x03 (V3 shape). The
    // envelope is a fully decodable stripped blob tx — newPayloadV3 flattens its versioned
    // hashes and requires expectedBlobVersionedHashes to name exactly them, in order.
    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 0;
    blob.maxPriorityFeePerGas = u256(1);
    blob.maxFeePerGas = u256(2);
    blob.gasLimit = 21000;
    blob.to = Address("2222222222222222222222222222222222222222");
    blob.value = u256(0);
    blob.maxFeePerBlobGas = u256(1);
    auto const versionedHash =
        h256("0101010101010101010101010101010101010101010101010101010101010101");
    blob.blobVersionedHashes = {versionedHash};
    blob.signatureR.assign(32, byte{0x12});
    blob.signatureS.assign(32, byte{0x34});
    blob.signatureV = 1;
    auto request = makeExternalRequestV2(
        c_headHash, c_headNumber + 1, /*rawTxs=*/{blob.encode()});
    request.executionPayload.blobGasUsed = u256(0);
    request.executionPayload.excessBlobGas = u256(0);
    request.parentBeaconBlockRoot =
        h256("5555555555555555555555555555555555555555555555555555555555555555");
    request.expectedBlobVersionedHashes = {versionedHash};
    request.executionPayload.blockHash =
        protocol::ethHeaderHash(expectedEthHeader(request));
    auto status = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK(status.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 1);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadV4ExecutionRequestsReachVerifier, ExternalPayloadFixture)
{
    seedHead();
    // EL mode: V4 executionRequests must be present and may be non-empty (EIP-7685).
    auto request = makeExternalRequestV4(
        c_headHash, c_headNumber + 1, {bytes{0x00, 0x11}, bytes{0x02, 0x22, 0x33}});
    auto status = task::syncWait(service.newPayload(request, 4));
    BOOST_CHECK(status.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(mockVerifier->calls, 1);
    BOOST_REQUIRE(mockVerifier->lastBlock->ethHeader.requestsHash.has_value());
    BOOST_CHECK_EQUAL(mockVerifier->lastBlock->ethHeader.requestsHash->hex(),
        expectedEthHeader(request).requestsHash->hex());
}

// --- CL-driven coordination (Phase 2): SYNCING answers post backfill targets, and the
// first served forkchoiceUpdated latches CL-driven mode. ---

BOOST_FIXTURE_TEST_CASE(externalPayloadHeightGapRequestsBackfill, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 3);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
    // The gap ahead posts the payload itself as the backfill target.
    BOOST_REQUIRE(clSync->backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*clSync->backfillTarget(), request.executionPayload.blockHash);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadUnknownParentRequestsBackfill, ExternalPayloadFixture)
{
    seedHead();
    auto request = makeExternalRequestV2(
        h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"), c_headNumber + 1);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 0);
    BOOST_REQUIRE(clSync->backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*clSync->backfillTarget(), request.executionPayload.blockHash);
}

BOOST_FIXTURE_TEST_CASE(externalPayloadStaleCommitRequestsBackfill, ExternalPayloadFixture)
{
    seedHead();
    mockVerifier->result = engine_common::ExternalPayloadResult{
        .outcome = engine_common::ExternalPayloadOutcome::StaleOrOutOfOrder, .error = {}};
    auto request = makeExternalRequestV2(c_headHash, c_headNumber + 1);
    auto status = task::syncWait(service.newPayload(request, 2));
    BOOST_CHECK(status.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(mockVerifier->calls, 1);
    BOOST_REQUIRE(clSync->backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*clSync->backfillTarget(), request.executionPayload.blockHash);
}

// FCU head the node has never seen: SYNCING, and the head hash becomes the backfill
// target. Serving the FCU at all latches CL-driven mode.
BOOST_FIXTURE_TEST_CASE(fcuUnknownHeadSyncingAndRequestsBackfill, ExternalPayloadFixture)
{
    seedHead();
    auto const unknownHead =
        h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    ForkchoiceState state{unknownHead, h256{}, h256{}};
    auto result = task::syncWait(service.updateForkchoice(state, nullptr, 3));
    BOOST_CHECK(result.payloadStatus.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_CHECK(clSync->clDriving());
    BOOST_REQUIRE(clSync->backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*clSync->backfillTarget(), unknownHead);
}

// The CL's head may lag the committed tip (the sync loop commits blocks the CL has not
// voted on yet): a canonical head behind the tip is accepted (VALID) and never
// rewound. A later FCU may then JUMP across the synced blocks to the tip — the
// EL-mode tracker relaxation (allowCanonicalHeadJump) is what admits it.
BOOST_FIXTURE_TEST_CASE(fcuHeadBehindTipAcceptedThenJumpsToTip, ExternalPayloadFixture)
{
    seedHead();
    auto const olderHash =
        h256("3333333333333333333333333333333333333333333333333333333333333333");
    storageFixture.setCanonicalBlock(olderHash, c_headNumber - 2);

    ForkchoiceState behind{olderHash, h256{}, h256{}};
    auto result = task::syncWait(service.updateForkchoice(behind, nullptr, 3));
    BOOST_CHECK(result.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(result.payloadStatus.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*result.payloadStatus.latestValidHash, olderHash);
    BOOST_CHECK(clSync->clDriving());
    BOOST_CHECK(!clSync->backfillTarget().has_value());

    // Tracked head is at c_headNumber - 2; the tip is c_headNumber. The jump (+2) is
    // canonical, so the EL lane accepts it where the strict +1 rule would reject.
    ForkchoiceState tip{c_headHash, h256{}, h256{}};
    auto jumped = task::syncWait(service.updateForkchoice(tip, nullptr, 3));
    BOOST_CHECK(jumped.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(jumped.payloadStatus.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*jumped.payloadStatus.latestValidHash, c_headHash);
}

// Phase 3 rewires the behind-tip case: a canonical head behind the committed tip now
// REWINDS the chain to that head (IExternalPayloadVerifier::rollbackToCommitted), which
// makes it the new tip — so payloadAttributes on it start a build (geth's
// SetCanonical-then-build) instead of failing with InvalidPayloadAttributes.
BOOST_FIXTURE_TEST_CASE(fcuAttributesOnBehindTipHeadRewindsAndBuilds, ExternalPayloadFixture)
{
    seedHead();
    auto const olderHash =
        h256("3333333333333333333333333333333333333333333333333333333333333333");
    storageFixture.setCanonicalBlock(olderHash, c_headNumber - 2);
    // The EL build lane reads the parent's header row for the L1 context derivation.
    seedHeaderRow(c_headNumber - 2);

    auto attributes = makePayloadAttributesV3();
    ForkchoiceState behind{olderHash, h256{}, h256{}};
    auto result = task::syncWait(service.updateForkchoice(behind, &attributes, 3));
    BOOST_CHECK(result.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK(result.payloadId.has_value());
    BOOST_CHECK_EQUAL(mockVerifier->rollbackCalls, 1u);
    BOOST_REQUIRE(mockVerifier->lastRollbackTarget.has_value());
    BOOST_CHECK_EQUAL(*mockVerifier->lastRollbackTarget, c_headNumber - 2);
    // The build went through the L1 lane: context derived from the parent header, block
    // executed by the (mock) shared execution phase.
    BOOST_CHECK_EQUAL(mockVerifier->deriveCalls, 1u);
    BOOST_REQUIRE(mockVerifier->lastDeriveParent.has_value());
    BOOST_CHECK_EQUAL(mockVerifier->lastDeriveParent->number, c_headNumber - 2);
    BOOST_CHECK_EQUAL(mockVerifier->buildCalls, 1u);

    // A follow-up FCU without attributes still applies and answers VALID.
    auto followUp = task::syncWait(service.updateForkchoice(behind, nullptr, 3));
    BOOST_CHECK(followUp.payloadStatus.status == PayloadValidationStatus::Valid);
}

// When the rewind is refused (beyond the reorg window or a missing journal row) the local
// state cannot serve the head: the CL must backfill from the network, so the answer is
// SYNCING and no payload is built — with or without attributes.
BOOST_FIXTURE_TEST_CASE(fcuAttributesRollbackRefusedAnswersSyncing, ExternalPayloadFixture)
{
    seedHead();
    auto const olderHash =
        h256("3333333333333333333333333333333333333333333333333333333333333333");
    storageFixture.setCanonicalBlock(olderHash, c_headNumber - 2);
    mockVerifier->rollbackResult = engine_common::ExternalRollbackResult{
        .rolledBack = false, .error = "beyond reorg window"};

    auto attributes = makePayloadAttributesV3();
    ForkchoiceState behind{olderHash, h256{}, h256{}};
    auto result = task::syncWait(service.updateForkchoice(behind, &attributes, 3));
    BOOST_CHECK(result.payloadStatus.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_CHECK_EQUAL(mockVerifier->rollbackCalls, 1u);
    BOOST_REQUIRE(mockVerifier->lastRollbackTarget.has_value());
    BOOST_CHECK_EQUAL(*mockVerifier->lastRollbackTarget, c_headNumber - 2);
}

BOOST_AUTO_TEST_SUITE_END()
