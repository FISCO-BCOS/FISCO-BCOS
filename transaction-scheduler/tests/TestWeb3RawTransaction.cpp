/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file TestWeb3RawTransaction.cpp
 * @brief Raw EIP-2718 -> Transaction decoder (decodeWeb3RawTransaction, the shared
 *        path for eth_sendRawTransaction / devp2p sync / external-block verification):
 *        build a REAL signed EIP-1559 tx, splice its raw bytes, decode them back and
 *        check every field + the recovered sender, then execute the decoded tx.
 * @date 2026/8/18
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/EthereumHost.h"
#include "EthereumBlockHashLookup.h"
#include <bcos-devp2p/rlpx/Crypto.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Anonymous namespace + TRW prefix: this TU is compiled standalone.
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;

using TWMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using TWBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using TWCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, TWBackendStorage>;
using TWMultiLayerStorage = MultiLayerStorage<TWMutableStorage, void, TWCheckpointBackend>;

class TRWTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

evmc_address TRWAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

// Ethereum address from an uncompressed (64-byte) secp256k1 public key.
bcos::bytes TRWAddressFromPublicKey(bcos::bytes const& publicKey)
{
    auto pubHash = bcos::crypto::keccak256Hash(
        bcos::bytesConstRef(publicKey.data(), publicKey.size()));
    return bcos::bytes(pubHash.begin() + 12, pubHash.end());
}

/// Build a REAL signed EIP-1559 value-transfer tx (0x02 envelope): the signing payload is
/// the canonical EIP-1559 field order, signed with a fresh keypair; the raw wire bytes are
/// what decodeWeb3RawTransaction must parse back.
std::shared_ptr<TRWTestTransactionImpl> TRWMakeSignedTransferTx(
    bcos::devp2p::rlpx::EccKeyPair const& keyPair, evmc_address const& recipient, uint64_t value,
    bcos::bytes& rawOut)
{
    auto tx = std::make_shared<TRWTestTransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.data.version = 1;
    inner.data.to = bcos::toHexStringWithPrefix(
        bcos::bytes(std::begin(recipient.bytes), std::end(recipient.bytes)));
    inner.data.blockLimit = 1000;
    inner.data.chainID = "0x1";
    inner.data.nonce = "0";
    inner.data.value = [&] {
        std::ostringstream oss;
        oss << "0x" << std::hex << value;
        return oss.str();
    }();
    inner.data.gasPrice = "0x0";
    inner.data.gasLimit = 100000;
    inner.data.maxFeePerGas = "0x3b9aca00";  // 1e9
    inner.data.maxPriorityFeePerGas = "0x0";
    inner.type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = 2;  // EIP-1559

    // Signing payload: 0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
    // gasLimit, to, value, data, accessList]) — the canonical EIP-1559 shape.
    bcos::bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));            // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));            // nonce
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));            // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1000000000));   // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));       // gasLimit
    bcos::codec::rlp::encode(
        body, bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes))));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(value));  // value
    bcos::codec::rlp::encode(body, bcos::bytes{});                 // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);              // empty accessList
    bcos::bytes payloadBytes;
    payloadBytes.push_back(0x02);
    bcos::codec::rlp::encodeHeader(payloadBytes,
        bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payloadBytes.insert(payloadBytes.end(), body.begin(), body.end());

    // Real signature: 65 bytes r || s || recid over keccak(payload).
    auto signingHash = bcos::crypto::keccak256Hash(
        bcos::bytesConstRef(payloadBytes.data(), payloadBytes.size()));
    auto const& privateKey = keyPair.privateKey();
    auto signature = bcos::devp2p::rlpx::signRecoverable(
        bcos::bytesConstRef(signingHash.data(), signingHash.size()),
        bcos::bytesConstRef(privateKey.data(), privateKey.size()));
    BOOST_REQUIRE_EQUAL(signature.size(), 65u);

    inner.extraTransactionBytes.assign(payloadBytes.begin(), payloadBytes.end());
    inner.signature.assign(signature.begin(), signature.end());

    // The sender is the keypair's Ethereum address (also what recovery must reproduce).
    tx->forceSender(TRWAddressFromPublicKey(keyPair.publicKey()));
    tx->calculateHash(*bcos::test::createNormalCryptoSuite()->hashImpl());
    tx->markClean();

    rawOut = bcostars::protocol::reassembleWeb3RawTransaction(
        tx->extraTransactionBytes(), tx->signatureData());
    return tx;
}

task::Task<void> TRWFundAccount(
    TWBackendStorage& storage, bcos::bytes const& senderRaw, u256 balance)
{
    using namespace bcos::ledger::account;
    evmc_address sender{};
    std::copy(senderRaw.begin(), senderRaw.end(), sender.bytes);
    EVMAccount<TWBackendStorage> acc(storage, sender, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

class TRWFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    TWBackendStorage backendStorage;
    TWCheckpointBackend checkpointBackend{backendStorage};
    TWMultiLayerStorage multiLayerStorage{checkpointBackend};
    eth::BlockHashLookup blockHashLookup;
    std::shared_ptr<EthereumExecutor> executor;
    bcos::protocol::BlockFactory::Ptr blockFactory;

    TRWFixture()
    {
        blockHashLookup = [&backend = backendStorage](
                              int64_t blockNumber, int64_t currentHeight) -> evmc::bytes32 {
            return initializer::ethBlockHashLookupFromStorage(
                backend, blockNumber, currentHeight);
        };
        executor = std::make_shared<EthereumExecutor>(receiptFactory, blockHashLookup);
        blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3RawTransactionTest)

// Round-trip a real signed EIP-1559 tx through decodeWeb3RawTransaction: every field, the
// recovered sender and the canonical hash must come back intact, and the decoded tx must
// execute successfully against a funded sender.
BOOST_FIXTURE_TEST_CASE(decodeSignedRawTransactionRoundTrip, TRWFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testTRW");
        SchedulerSerialImpl scheduler(ioServicePool);

        bcos::devp2p::rlpx::EccKeyPair keyPair;
        auto senderRaw = TRWAddressFromPublicKey(keyPair.publicKey());
        auto recipient = TRWAddress(0x21);
        BOOST_REQUIRE_EQUAL(senderRaw.size(), 20u);

        co_await TRWFundAccount(backendStorage, senderRaw, u256(1000000000000000000ULL));

        bcos::bytes raw;
        auto original = TRWMakeSignedTransferTx(keyPair, recipient, 100, raw);
        BOOST_REQUIRE(!raw.empty());

        // The decoder is the shared raw->Transaction path.
        auto decoded = bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), *cryptoSuite->hashImpl());
        BOOST_REQUIRE(decoded);

        // Field-level round-trip.
        BOOST_CHECK_EQUAL(decoded->web3TypedTxKind(), 2u);
        BOOST_CHECK_EQUAL(decoded->to(), bcos::toHexStringWithPrefix(
                                             bcos::bytes(std::begin(recipient.bytes),
                                                 std::end(recipient.bytes))));
        BOOST_CHECK_EQUAL(decoded->value(), u256(100));
        BOOST_CHECK_EQUAL(decoded->gasLimit(), 100000);
        BOOST_CHECK_EQUAL(decoded->nonce(), std::string_view("0x0"));
        BOOST_CHECK(decoded->maxFeePerGas().has_value() &&
                    *decoded->maxFeePerGas() == u256(1000000000));
        // Recovered sender must be the keypair's Ethereum address.
        auto decodedSender = decoded->sender();
        BOOST_CHECK_EQUAL(bcos::bytes(decodedSender.begin(), decodedSender.end()), senderRaw);
        // Canonical tx hash: keccak of the raw wire bytes.
        auto canonical = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(raw.data(), raw.size()));
        BOOST_CHECK(decoded->hash() == canonical);
        BOOST_CHECK(decoded->hash() == original->hash());

        // Execute the DECODED tx: the recovered sender is funded, so it must succeed.
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setNumber(1);
        blockHeader.setTimestamp(1700000000000LL);
        blockHeader.setParentInfo({0, bcos::crypto::HashType{}});
        blockHeader.setGasLimit(u256(30000000));
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        ledgerConfig.setEVMCRevision(EVMC_SHANGHAI);
        ledgerConfig.setGasLimit({30000000, 1});
        ledgerConfig.setGasPrice({"0x3b9aca00", 1});  // 1e9
        ledgerConfig.setDifficulty(0);

        auto view = multiLayerStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{decoded};
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, blockHeader, txs | ::ranges::views::indirect, ledgerConfig);
        BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);
        BOOST_CHECK_EQUAL(receipts[0]->gasUsed(), u256(21000));
    }());
}

// Decode REAL on-chain Sepolia transactions (fetched from the archive RPC, verified
// offline with eth-sync-check --verify-tx / --raw-tx) through the shared decoder:
//   block 800000 tx 0: EIP-1559 transfer, sender 0xcfe958...a5a3, value 0x5d423c655aa0000
//   block 7200000 tx 0: EIP-4844 blob (EigenLayer beacon deposit), 2 versioned hashes
// These pin the decoder against bytes produced by geth on the live chain.
BOOST_FIXTURE_TEST_CASE(decodeRealSepoliaTransactions, TRWFixture)
{
    // EIP-1559 transfer, Sepolia block 800000 (raw from eth_getRawTransactionByHash).
    bcos::bytes raw1559 = bcos::fromHex(std::string_view(
        "02f87783aa36a7820ba58477359400847735940782520894274cde4f18c828e6e8b53bdb25fa"
        "b3676dffbfb18805d423c655aa000080c080a0e254110c8cae14cf34698ba29ae58b1f51db4d"
        "13438dffd4c344a63c68d43f79a0797fba5d57edefa9d09a5ef8d1dcbf021791d3c2f295a7b9"
        "c3487232df901451"));
    auto decoded1559 = bcos::rpc::decodeWeb3RawTransaction(
        bcos::bytesConstRef(raw1559.data(), raw1559.size()), *cryptoSuite->hashImpl());
    BOOST_REQUIRE(decoded1559);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(decoded1559->web3TypedTxKind()), 2u);
    BOOST_CHECK(decoded1559->hash() == bcos::crypto::HashType(std::string_view(
        "d2dc9ce65a21ad744b39974697e6a2b9f41381ea798dbdf9cea75c526879c524"),
        bcos::crypto::HashType::FromHex));
    BOOST_CHECK_EQUAL(bcos::toHexStringWithPrefix(bcos::bytes(decoded1559->sender().begin(),
                          decoded1559->sender().end())),
        std::string("0xcfe95817ac44c3f8ce75f1ee6ec1431f586ab5a3"));
    BOOST_CHECK_EQUAL(decoded1559->to(),
        std::string_view("0x274cde4f18c828e6e8b53bdb25fab3676dffbfb1"));
    BOOST_CHECK_EQUAL(decoded1559->value(), u256("0x5d423c655aa0000"));
    BOOST_CHECK_EQUAL(decoded1559->nonce(), std::string_view("0xba5"));
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(decoded1559->gasLimit()), 21000u);
    BOOST_CHECK(decoded1559->maxFeePerGas().has_value() &&
                *decoded1559->maxFeePerGas() == u256("0x77359407"));
    BOOST_CHECK(decoded1559->maxFeePerBlobGas().has_value() == false);

    // EIP-4844 blob tx, Sepolia block 7200000 (EigenLayer beacon deposit, 1 blob).
    bcos::bytes raw4844 = bcos::fromHex(std::string_view(
        "03f89883aa36a783018d76843b9aca0085018a917f4482520894256c2a2c24ca6afff72726b43"
        "dc9022469012c3e8080c0843b9aca00e1a0013fc3ae05167af931bb2a3329fedfaf13d61c728"
        "c593d3346968c8f258b224780a03e09517416805d15670bb8647bd83d927df348b939d0841ba"
        "5459d9cbfb8dfeda07288d56dfff3ee2ae65eca9e81bde0d6d3b5571ecd67a688c891c6f2bcb"
        "098e2"));
    auto decoded4844 = bcos::rpc::decodeWeb3RawTransaction(
        bcos::bytesConstRef(raw4844.data(), raw4844.size()), *cryptoSuite->hashImpl());
    BOOST_REQUIRE(decoded4844);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(decoded4844->web3TypedTxKind()), 3u);
    BOOST_CHECK(decoded4844->hash() == bcos::crypto::HashType(std::string_view(
        "58285232ac6e1a7538404a3ec910efbdb23eb0591971aa865df9d799c9458095"),
        bcos::crypto::HashType::FromHex));
    BOOST_CHECK_EQUAL(bcos::toHexStringWithPrefix(bcos::bytes(decoded4844->sender().begin(),
                          decoded4844->sender().end())),
        std::string("0x0c970bb76126ac18c024f8d7fe81aeaf3eefa82e"));
    BOOST_CHECK_EQUAL(decoded4844->to(),
        std::string_view("0x256c2a2c24ca6afff72726b43dc9022469012c3e"));
    BOOST_CHECK_EQUAL(decoded4844->nonce(), std::string_view("0x18d76"));
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(decoded4844->gasLimit()), 21000u);
    BOOST_CHECK(decoded4844->maxFeePerGas().has_value() &&
                *decoded4844->maxFeePerGas() == u256("0x18a917f44"));
    BOOST_CHECK(decoded4844->maxFeePerBlobGas().has_value() &&
                *decoded4844->maxFeePerBlobGas() == u256("0x3b9aca00"));
    auto const& blobs = decoded4844->blobVersionedHashes();
    BOOST_REQUIRE_EQUAL(blobs.size(), 1u);
    BOOST_CHECK(blobs[0] == bcos::h256(std::string_view(
        "013fc3ae05167af931bb2a3329fedfaf13d61c728c593d3346968c8f258b2247"),
        bcos::h256::FromHex));
}

BOOST_AUTO_TEST_SUITE_END()
