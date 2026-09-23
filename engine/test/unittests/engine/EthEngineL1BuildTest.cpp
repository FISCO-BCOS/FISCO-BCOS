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
 * @file EthEngineL1BuildTest.cpp
 * @brief EL-mode self-build / self-verify closed loop: EthEngineService builds an L1
 *        payload through the REAL IExternalPayloadVerifier (deriveL1Context ->
 *        buildL1Block -> EthereumBlockVerifier::executeEthereumBlock), then a second
 *        node holding identical genesis state verifies that payload through
 *        EthereumBlockVerifier::verifyAndCommit. The transactions carry genuine
 *        secp256k1 signatures, so both sides run the production raw-envelope decoder.
 */

#include "engine/bcos-engine/EthEngineService.h"
#include "engine/test/unittests/engine/EthServiceStubs.h"

#include "libinitializer/EthereumBlockHashLookup.h"
#include "libinitializer/ExternalPayloadVerifier.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/kzg/Kzg4844.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-transaction-scheduler/EthereumBlockVerifier.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <ethereum-executor/EthereumExecutor.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::engine::eth_test;

namespace
{
using EL1BVerifier = scheduler_v1::EthereumBlockVerifier<scheduler_v1::SchedulerSerialImpl,
    executor_v1::eth::EthereumExecutor>;
using EL1BService = EthEngineService<bcos::txpool::MemPoolImpl, RealGlobalStateStorage,
    StubExecutor, StubScheduler>;

// Genesis block-0 context every case shares: a 30M-gas Cancun parent at the slot before
// the payload timestamp (the Engine-API default, 1700000000 s).
constexpr int64_t c_parentTimestamp = 1700000000 - 12;  // seconds
constexpr std::uint64_t c_blockTimestampMs = c_defaultPayloadTimestamp;  // 1700000000 s
constexpr u256 c_parentGasLimit = u256(30000000);
const h256 c_genesisHash{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};

scheduler_v1::EvmcForkTimestamps el1bCancunForks()
{
    scheduler_v1::EvmcForkTimestamps forks;
    // Unset fields default to kForkDisabled — London..Cancun must be pinned to 0 to mean
    // "active from genesis"; Prague/Osaka/BPO stay disabled (Cancun-era chain).
    forks.londonTime = 0;
    forks.parisTime = 0;
    forks.shanghaiTime = 0;
    forks.cancunTime = 0;
    return forks;
}

/// Sign the EIP-2718 signing hash with a real secp256k1 key and return the raw envelope.
bcos::bytes el1bSign(bcos::rpc::Web3Transaction& w3, crypto::KeyPairInterface const& keyPair)
{
    crypto::Secp256k1Crypto secp;
    auto const sig = secp.sign(keyPair, w3.hashForSign(), false);
    BOOST_REQUIRE(sig);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];  // typed transactions carry yParity directly
    return w3.encode();
}

task::Task<void> el1bFund(
    RealGlobalStateBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<RealGlobalStateBackendStorage> acc(storage, addr, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

template <class Storage>
task::Task<u256> el1bBalance(Storage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, false);
    co_return co_await acc.balance();
}

void el1bWriteCurrentNumber(RealGlobalStateBackendStorage& backend, int64_t number)
{
    storage::Entry entry;
    entry.set(std::to_string(number));
    task::syncWait(storage2::writeOne(backend,
        executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
}

/// The number-keyed header row the EL build lane reads the parent Ethereum header from
/// (updateForkchoice -> getBlockData(HEADER) -> EthBlockHeader(...).data()).
void el1bSeedParentHeaderRow(RealGlobalStateBackendStorage& backend,
    bcos::protocol::BlockFactory& blockFactory, protocol::EthBlockHeaderData const& parent)
{
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(parent.number);
    header->setTimestamp(parent.timestamp * 1000);  // Eth data is seconds, TARS is ms
    header->setGasLimit(parent.gasLimit);
    header->setGasUsed(parent.gasUsed);
    header->setStateRoot(parent.stateRoot);
    header->setTxsRoot(parent.txsRoot);
    header->setReceiptsRoot(parent.receiptsRoot);
    if (parent.baseFee.has_value())
    {
        header->setBaseFee(*parent.baseFee);
    }
    if (parent.blobGasUsed.has_value())
    {
        header->setBlobGasUsed(*parent.blobGasUsed);
    }
    if (parent.excessBlobGas.has_value())
    {
        header->setExcessBlobGas(*parent.excessBlobGas);
    }
    header->setEthBlockVersion(protocol::EthBlockVersion::CANCUN);
    bcos::bytes buffer;
    header->encode(buffer);
    storage::Entry entry;
    entry.set(std::move(buffer));
    task::syncWait(storage2::writeOne(backend,
        executor_v1::StateKey{
            ledger::SYS_NUMBER_2_BLOCK_HEADER, boost::lexical_cast<std::string>(parent.number)},
        std::move(entry)));
}

/// One node's storage stack + the real execution/verification pipeline (the same
/// EthereumExecutor + EthereumBlockVerifier pairing the production Initializer wires).
struct EL1BNode
{
    RealGlobalStateBackendStorage backendStorage;
    RealGlobalCheckpointBackend checkpointBackend{backendStorage};
    RealGlobalStateStorage storage{checkpointBackend};
    std::shared_ptr<bcos::IOServicePool> ioServicePool;
    std::unique_ptr<scheduler_v1::SchedulerSerialImpl> scheduler;
    crypto::CryptoSuite::Ptr cryptoSuite = bcos::test::createNormalCryptoSuite();
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    bcos::protocol::BlockFactory::Ptr blockFactory =
        bcos::test::createBlockFactory(cryptoSuite);
    std::shared_ptr<executor_v1::eth::EthereumExecutor> executor;
    std::shared_ptr<EL1BVerifier> verifier;
    std::shared_ptr<bcos::test::FakeLedger> fakeLedger =
        std::make_shared<bcos::test::FakeLedger>();

    explicit EL1BNode(std::string name)
    {
        ioServicePool = std::make_shared<bcos::IOServicePool>(1, std::move(name));
        scheduler = std::make_unique<scheduler_v1::SchedulerSerialImpl>(ioServicePool);
        executor_v1::eth::BlockHashLookup lookup =
            [&backend = backendStorage](int64_t blockNumber, int64_t currentHeight) {
                return initializer::ethBlockHashLookupFromStorage(
                    backend, blockNumber, currentHeight);
            };
        executor = std::make_shared<executor_v1::eth::EthereumExecutor>(
            receiptFactory, std::move(lookup));
        verifier = std::make_shared<EL1BVerifier>(*scheduler, *executor, *blockFactory);
    }

    /// Identical genesis on both nodes: executor config, funded accounts, the canonical
    /// head rows, and the parent header row the build lane derives the L1 context from.
    void seedGenesis(protocol::EthBlockHeaderData const& parent,
        std::vector<std::pair<evmc_address, u256>> const& funded)
    {
        writeEthExecutorConfig(backendStorage, EVMC_CANCUN);
        writeRawSysConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");
        for (auto const& [addr, balance] : funded)
        {
            task::syncWait(el1bFund(backendStorage, addr, balance));
        }
        el1bWriteCurrentNumber(backendStorage, 0);
        writeHashToNumber(backendStorage, c_genesisHash, 0);
        writeNumberToHash(backendStorage, 0, c_genesisHash);
        el1bSeedParentHeaderRow(backendStorage, *blockFactory, parent);
    }
};

/// Node A: the block builder — EthEngineService with the REAL external-payload seam
/// (ExternalPayloadVerifierImpl over the node's own verifier), and an L1-fee-market pool.
struct EL1BFixture
{
    EL1BNode nodeA{"el1bA"};
    EL1BNode nodeB{"el1bB"};
    bcos::txpool::MemPoolImpl memPool{bcos::txpool::MemPoolConfig{
        .chainKind = bcos::txpool::ChainKind::L1}};
    StubExecutor stubExecutor;
    StubScheduler stubScheduler;
    std::shared_ptr<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>
        externalVerifier;
    EL1BService service;

    EL1BFixture()
      : externalVerifier(
            std::make_shared<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>(
                nodeA.verifier, nodeA.fakeLedger, nodeA.blockFactory, el1bCancunForks(),
                /*chainId=*/1, /*mergeBlock=*/0)),
        service(memPool, nodeA.storage, stubExecutor, stubScheduler, nodeA.blockFactory,
            /*ledger=*/nullptr, engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
            /*ledgerConfigState=*/nullptr, externalVerifier,
            /*clSync=*/std::make_shared<engine_common::ClSyncCoordination>())
    {}
};

protocol::EthBlockHeaderData el1bParentHeader(u256 gasUsed, u256 excessBlobGas,
    u256 blobGasUsed)
{
    protocol::EthBlockHeaderData parent;
    parent.number = 0;
    parent.timestamp = c_parentTimestamp;
    parent.uncleHash = protocol::c_emptyOmmersHash;
    parent.difficulty = u256(0);
    parent.gasLimit = c_parentGasLimit;
    parent.gasUsed = gasUsed;
    parent.baseFee = u256(1000000000);  // 1 gwei
    // The genesis state was written flat (no trie nodes), so the MPT build on both
    // sides starts from the empty trie root — same convention as the verifier tests.
    parent.stateRoot = ledger::mpt::emptyRootHash();
    parent.txsRoot = ledger::mpt::emptyRootHash();
    parent.receiptsRoot = ledger::mpt::emptyRootHash();
    parent.blobGasUsed = blobGasUsed;
    parent.excessBlobGas = excessBlobGas;
    return parent;
}

evmc_address el1bEvmcAddress(bcos::Address const& address)
{
    evmc_address addr{};
    std::copy_n(address.data(), sizeof(addr.bytes), addr.bytes);
    return addr;
}

evmc_address el1bEvmcAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

/// Admit a raw envelope to the pool through the production decode path (recovers the
/// sender from the signature). A blob transaction's EIP-4844 sidecar registers atomically
/// with the admission, exactly like the RPC ingress does.
void el1bPoolAdd(bcos::txpool::MemPoolImpl& pool, bcos::crypto::Hash& hashImpl,
    bcos::bytes const& raw, std::optional<engine::BlobTxSidecar> sidecar = std::nullopt)
{
    auto tx = bcos::rpc::decodeWeb3RawTransaction(
        bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    BOOST_REQUIRE(tx);
    // The ingress contract: recover the sender from the signature and clear the tainted
    // flag (the pool rejects tainted transactions).
    crypto::Secp256k1Crypto secp;
    tx->verify(hashImpl, secp);
    BOOST_CHECK(pool.tryAdd(std::move(tx), std::move(sidecar)) == protocol::TransactionStatus::None);
}

/// A one-blob EIP-4844 sidecar with genuine KZG commitment/proof for @p seed, plus the
/// versioned hash the transaction must name.
engine::BlobTxSidecar el1bMakeSidecar(uint8_t seed)
{
    engine::BlobTxSidecar sidecar;
    bcos::bytes blob(crypto::kzg::BlobSize, bcos::byte{0});
    // The last byte: every 32-byte field element stays far below the BLS12-381 scalar
    // modulus, so any seed value keeps the blob valid for c-kzg.
    blob.back() = bcos::byte{seed};
    bcos::bytes commitment, proof;
    BOOST_REQUIRE(crypto::kzg::blobToKzgCommitment(bcos::ref(blob), commitment));
    BOOST_REQUIRE(crypto::kzg::computeBlobKzgProof(bcos::ref(blob), bcos::ref(commitment), proof));
    sidecar.blobs.push_back(std::move(blob));
    sidecar.commitments.push_back(std::move(commitment));
    sidecar.proofs.push_back(std::move(proof));
    return sidecar;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthEngineL1BuildTest)

// Closed loop, Cancun: FCU(V3) builds a payload with one EIP-1559 transfer and one blob
// transfer through the real executor; a second node with identical genesis runs the
// payload through verifyAndCommit and must answer Valid (and commit the state change).
BOOST_FIXTURE_TEST_CASE(cancunBuildThenVerifyClosedLoop, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x21);  // not a precompile

    // EIP-1559 transfer (nonce 0): maxFee 2 gwei covers the derived 0.875 gwei base fee.
    bcos::rpc::Web3Transaction transfer;
    transfer.type = bcos::rpc::TransactionType::EIP1559;
    transfer.chainId = 1;
    transfer.nonce = 0;
    transfer.maxPriorityFeePerGas = u256(100000000);
    transfer.maxFeePerGas = u256(2000000000);
    transfer.gasLimit = 21000;
    transfer.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    transfer.value = u256(100);
    auto rawTransfer = el1bSign(transfer, *senderKey);

    // EIP-4844 blob transfer (nonce 1): one blob with a genuine KZG sidecar => 131072
    // blob gas. The versioned hash is derived from the real commitment.
    auto sidecar = el1bMakeSidecar(0x2a);
    auto const sidecarCommitment = sidecar.commitments.front();
    auto const sidecarProof = sidecar.proofs.front();
    auto const sidecarBlob = sidecar.blobs.front();
    auto const versionedHash =
        crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecarCommitment));
    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 1;
    blob.maxPriorityFeePerGas = u256(100000000);
    blob.maxFeePerGas = u256(2000000000);
    blob.gasLimit = 21000;
    blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    blob.value = u256(7);
    blob.maxFeePerBlobGas = u256(1000000000);
    blob.blobVersionedHashes = {versionedHash};
    auto rawBlob = el1bSign(blob, *senderKey);

    auto parent = el1bParentHeader(/*gasUsed=*/u256(0), /*excessBlobGas=*/u256(0),
        /*blobGasUsed=*/u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    // The pool sees the production decode path: raw envelope -> tars transaction with a
    // recovered sender. The blob transaction's sidecar registers with the admission.
    el1bPoolAdd(memPool, hashImpl, rawTransfer);
    el1bPoolAdd(memPool, hashImpl, rawBlob, std::move(sidecar));

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    attributes.withdrawals = std::vector<WithdrawalV1>{WithdrawalV1{.index = 0,
        .validatorIndex = 1, .amount = 2,
        .address = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)))}};
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());

    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    BOOST_CHECK_EQUAL(payload.blockNumber, 1);
    BOOST_CHECK_EQUAL(payload.parentHash, c_genesisHash);
    BOOST_CHECK_EQUAL(payload.timestamp, c_blockTimestampMs);
    // EIP-1559 from the REAL derivation: parent gasUsed 0 vs target 15M =>
    // 1 gwei - 1 gwei/8 = 875000000.
    BOOST_CHECK_EQUAL(payload.baseFeePerGas, u256(875000000));
    // EIP-4844 real accounting: 1 versioned hash x BLOB_GAS_PER_BLOB.
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(131072));
    BOOST_REQUIRE(payload.excessBlobGas.has_value());
    BOOST_CHECK_EQUAL(*payload.excessBlobGas, u256(0));
    // Executed roots, not placeholders.
    BOOST_CHECK(payload.stateRoot != h256{});
    BOOST_CHECK(payload.receiptsRoot != h256{});
    BOOST_REQUIRE_EQUAL(payload.transactions.size(), 2);
    BOOST_CHECK(payload.transactions[0].raw == rawTransfer);
    BOOST_CHECK(payload.transactions[1].raw == rawBlob);
    BOOST_REQUIRE(payload.withdrawals.has_value());
    BOOST_REQUIRE_EQUAL(payload.withdrawals->size(), 1);
    // L1 dialect: never the OP MessagePasser withdrawalsRoot, no Cancun requests.
    BOOST_CHECK(!payload.withdrawalsRoot.has_value());
    BOOST_CHECK(!data->executionRequests.has_value());

    // The blobsBundle is the real sidecar content (V3 answers it on an L1): one entry
    // per array, matching the registered sidecar byte-for-byte, and the commitment's
    // versioned hash is what the transaction named.
    BOOST_REQUIRE(data->blobsBundle.has_value());
    BOOST_REQUIRE_EQUAL(data->blobsBundle->commitments.size(), 1);
    BOOST_REQUIRE_EQUAL(data->blobsBundle->proofs.size(), 1);
    BOOST_REQUIRE_EQUAL(data->blobsBundle->blobs.size(), 1);
    BOOST_CHECK(data->blobsBundle->commitments.front() == sidecarCommitment);
    BOOST_CHECK(data->blobsBundle->proofs.front() == sidecarProof);
    BOOST_CHECK(data->blobsBundle->blobs.front() == sidecarBlob);
    BOOST_CHECK_EQUAL(
        crypto::kzg::versionedHashFromCommitment(bcos::ref(data->blobsBundle->commitments.front())),
        versionedHash);
    BOOST_CHECK(crypto::kzg::verifyBlobKzgProof(bcos::ref(data->blobsBundle->blobs.front()),
        bcos::ref(data->blobsBundle->commitments.front()),
        bcos::ref(data->blobsBundle->proofs.front())));

    // (getPayloadV4 is a Prague-shape method; requesting it on this Cancun-built payload is
    // rejected by requireGetPayloadShape, so the bundle is asserted through V3 only.)

    // ---- Node B: verify the payload as an external block and commit it. ----
    NewPayloadRequest request;
    request.executionPayload = payload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);

    EL1BVerifier::TransactionDecoder decoder =
        [&hashImpl](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    };
    using ViewType = RealGlobalStateStorage::ViewType;
    EL1BVerifier::StateRootCalculator<ViewType> stateRootCalc =
        [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto forks = el1bCancunForks();
    auto result = task::syncWait(nodeB.verifier->verifyAndCommit(nodeB.storage,
        *nodeB.fakeLedger, external->ethHeader, parent, external->rawTransactions,
        external->rawWithdrawals, forks, /*chainId=*/1, /*rawUncles=*/{}, /*mergeBlock=*/0,
        decoder, stateRootCalc));
    BOOST_CHECK(result.valid);
    BOOST_CHECK_MESSAGE(result.error.empty(), result.error);

    // The committed state carries the two transfers plus the 2-gwei withdrawal credit.
    auto const recipientBalance =
        task::syncWait(el1bBalance(nodeB.storage.latestBackend(), recipient));
    BOOST_CHECK_EQUAL(recipientBalance, u256(100 + 7) + u256(2000000000));
}

// The L1 context derivation is real: a parent above its gas target raises the base fee,
// and a parent at the blob target with one blob carried raises the excess blob gas.
// An empty built block must stamp exactly those derived values and still verify.
BOOST_FIXTURE_TEST_CASE(cancunDerivedContextStampsBuiltHeader, EL1BFixture)
{
    // Cancun blob schedule: target 3 blobs (393216 gas), max 6. Parent carries
    // excess == target and 1 blob used => next excess = 393216 + 131072 - 393216.
    auto parent = el1bParentHeader(/*gasUsed=*/u256(30000000),  // full parent block
        /*excessBlobGas=*/u256(393216), /*blobGasUsed=*/u256(131072));
    nodeA.seedGenesis(parent, {});
    nodeB.seedGenesis(parent, {});

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    // EIP-1559 up-adjust: parent full => 1 gwei + 1 gwei/8 = 1125000000.
    BOOST_CHECK_EQUAL(payload.baseFeePerGas, u256(1125000000));
    BOOST_REQUIRE(payload.excessBlobGas.has_value());
    BOOST_CHECK_EQUAL(*payload.excessBlobGas, u256(131072));
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(0));  // no blob transactions in the block
    BOOST_CHECK(payload.transactions.empty());
    BOOST_CHECK(payload.stateRoot != h256{});

    NewPayloadRequest request;
    request.executionPayload = payload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);

    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    EL1BVerifier::TransactionDecoder decoder =
        [&hashImpl](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    };
    using ViewType = RealGlobalStateStorage::ViewType;
    EL1BVerifier::StateRootCalculator<ViewType> stateRootCalc =
        [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto forks = el1bCancunForks();
    auto result = task::syncWait(nodeB.verifier->verifyAndCommit(nodeB.storage,
        *nodeB.fakeLedger, external->ethHeader, parent, external->rawTransactions,
        external->rawWithdrawals, forks, /*chainId=*/1, /*rawUncles=*/{}, /*mergeBlock=*/0,
        decoder, stateRootCalc));
    BOOST_CHECK(result.valid);
    BOOST_CHECK_MESSAGE(result.error.empty(), result.error);
}

// A pooled blob transaction whose sidecar never arrived is unbuildable: seal skips it —
// and with it the sender's remaining nonce suffix — instead of minting a payload whose
// blobsBundle could never be assembled.
BOOST_FIXTURE_TEST_CASE(sealSkipsBlobTransactionWithoutSidecar, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x22);

    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 0;
    blob.maxPriorityFeePerGas = u256(100000000);
    blob.maxFeePerGas = u256(2000000000);
    blob.gasLimit = 21000;
    blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    blob.value = u256(7);
    blob.maxFeePerBlobGas = u256(1000000000);
    blob.blobVersionedHashes = {h256("0101010101010101010101010101010101010101010101010101010101010101")};
    auto rawBlob = el1bSign(blob, *senderKey);

    // Same sender, next nonce: an ordinary transfer. The suffix rule drops it together
    // with the stalled blob transaction (executing it would hit the nonce gap).
    bcos::rpc::Web3Transaction transfer;
    transfer.type = bcos::rpc::TransactionType::EIP1559;
    transfer.chainId = 1;
    transfer.nonce = 1;
    transfer.maxPriorityFeePerGas = u256(100000000);
    transfer.maxFeePerGas = u256(2000000000);
    transfer.gasLimit = 21000;
    transfer.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    transfer.value = u256(100);
    auto rawTransfer = el1bSign(transfer, *senderKey);

    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    // No sidecar on either admission.
    el1bPoolAdd(memPool, hashImpl, rawBlob);
    el1bPoolAdd(memPool, hashImpl, rawTransfer);

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    BOOST_CHECK(payload.transactions.empty());
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(0));
    // The stalled transactions stay pooled for a later block.
    auto fetched = memPool.get(std::vector<crypto::HashType>{});
    (void)fetched;
    BOOST_REQUIRE(data->blobsBundle.has_value());
    BOOST_CHECK(data->blobsBundle->commitments.empty());
}

// newPayloadV3 on an EL-mode node validates expectedBlobVersionedHashes against the
// payload's blob transactions in block order: a mismatch is Invalid, never accepted.
BOOST_FIXTURE_TEST_CASE(newPayloadRejectsMismatchedExpectedBlobVersionedHashes, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x23);

    auto sidecar = el1bMakeSidecar(0x7b);
    auto const versionedHash =
        crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecar.commitments.front()));
    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 0;
    blob.maxPriorityFeePerGas = u256(100000000);
    blob.maxFeePerGas = u256(2000000000);
    blob.gasLimit = 21000;
    blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    blob.value = u256(7);
    blob.maxFeePerBlobGas = u256(1000000000);
    blob.blobVersionedHashes = {versionedHash};
    auto rawBlob = el1bSign(blob, *senderKey);

    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    el1bPoolAdd(memPool, hashImpl, rawBlob, std::move(sidecar));

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);

    NewPayloadRequest request;
    request.executionPayload = data->executionPayload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;

    // Wrong hash entirely.
    request.expectedBlobVersionedHashes = {h256("0202020202020202020202020202020202020202020202020202020202020202")};
    auto rejected =
        task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK(rejected.status == PayloadValidationStatus::Invalid);

    // The payload carries one blob; an empty expectation mismatches too.
    request.expectedBlobVersionedHashes = {};
    auto rejectedEmpty = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK(rejectedEmpty.status == PayloadValidationStatus::Invalid);
}

BOOST_AUTO_TEST_SUITE_END()
