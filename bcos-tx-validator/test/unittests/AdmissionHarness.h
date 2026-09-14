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
 * @file AdmissionHarness.h
 * @brief One TxValidator harness for every admission test file.
 *
 * The module is stateless and every dependency is injected, so these run against fakes: no
 * transaction pool, no block sequence, no scheduler unless a case wants one. A test file adds
 * cases, never a second copy of this -- two harnesses drift the moment one of them changes its
 * default revision or its funding, and both files stay green while they disagree.
 *
 * `ledgerConfig` is the SETUP config, not the served one: a case edits it freely (gas price,
 * revision, chain id, features) between construction and run(), and run() then publishes a fresh
 * copy through LedgerConfigState::set() -- the same call a committing node makes. So what the
 * validator reads is an immutable snapshot, as in production, and no case can mutate a
 * configuration that has already been published. A case wanting the never-published state (the
 * gap between a node's construction and its first commit) clears `ledgerConfig`.
 */

#pragma once

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/testutils/faker/FakeScheduler.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tx-validator/LedgerNonceChecker.h"
#include "bcos-tx-validator/TxValidator.h"
#include "bcos-tx-validator/Web3NonceChecker.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <optional>
#include <string>

namespace bcos::test
{
/// Scoped to this namespace, not global: everything below names protocol and validator types
/// unqualified, and every file that includes this puts its cases in bcos::test too.
using namespace bcos::protocol;
using namespace bcos::txvalidator;

inline constexpr uint64_t kChainId = 5;

/// One key pair for the whole suite, so `sender` is stable across cases.
inline crypto::Secp256k1Crypto& signer()
{
    static crypto::Secp256k1Crypto instance;
    return instance;
}
inline crypto::KeyPairInterface& senderKey()
{
    static auto keyPair = signer().generateKeyPair();
    return *keyPair;
}

/// Build and genuinely SIGN a Web3 transaction, then project it to tars the way the RPC ingress
/// does.
///
/// Signing here rather than using a static raw-transaction vector is deliberate: the RLP suite's
/// vectors exist to round-trip the codec, so several carry arbitrary r/s that do not recover to
/// any address -- and admit() runs signature recovery. Signing also makes gasLimit, chainId and
/// the fee fields free variables, which several cases below need; a fixed vector cannot be
/// edited without invalidating its own signature.
struct TxSpec
{
    rpc::TransactionType type = rpc::TransactionType::EIP1559;
    std::optional<uint64_t> chainId = kChainId;
    uint64_t nonce = 7;
    uint64_t gasLimit = 100000;
    u256 maxFeePerGas = 30000000000ULL;
    u256 maxPriorityFeePerGas = 1000000000ULL;
    std::optional<Address> to = Address{"0x811a752c8cd697e3cb27279c330ed1ada745a8d7"};
    bcos::bytes data{};
    u256 value = 1;
    bool withAuthorization = false;
    /// Zero out r after signing: the envelope stays self-consistent (the hash commits to the
    /// signature bytes as sent) but recovery is impossible, so the rejection is the signature
    /// check's own and not normalization's.
    bool unrecoverableSignature = false;
    /// Replace s by n - s after signing: the same envelope under the other of the two signatures
    /// every ECDSA message has. Recovery succeeds on it (to a different address), and only the
    /// EIP-2 low-s rule tells the twins apart.
    bool highS = false;
};

inline std::shared_ptr<bcostars::protocol::TransactionImpl> admitTx(TxSpec const& spec = {})
{
    rpc::Web3Transaction web3;
    web3.type = spec.type;
    web3.chainId = spec.chainId;
    web3.nonce = spec.nonce;
    web3.gasLimit = spec.gasLimit;
    web3.maxFeePerGas = spec.maxFeePerGas;
    web3.maxPriorityFeePerGas = spec.maxPriorityFeePerGas;
    web3.to = spec.to;
    web3.data = spec.data;
    web3.value = spec.value;
    if (spec.withAuthorization)
    {
        rpc::AuthorizationListEntry entry;
        entry.chainId = kChainId;
        entry.address = Address{"0x00000000000000000000000000000000000000aa"};
        web3.authorizationList.push_back(entry);
    }

    crypto::Keccak256 hasher;
    auto const signHash = web3.hashForSign();
    auto signature = signer().sign(senderKey(), signHash, true);
    BOOST_REQUIRE_EQUAL(signature->size(), 65U);
    web3.signatureR.assign(signature->begin(), signature->begin() + 32);
    web3.signatureS.assign(signature->begin() + 32, signature->begin() + 64);
    web3.signatureV = static_cast<uint64_t>((*signature)[64]);
    if (spec.highS)
    {
        auto const s = fromBigEndian<u256>(web3.signatureS);
        BOOST_REQUIRE_LT(s, crypto::c_secp256k1nOver2);  // the signer emits low-s; the twin is high
        bcos::bytes twin(32, 0);
        toBigEndian(crypto::c_secp256k1n - s, twin);
        web3.signatureS = std::move(twin);
    }
    if (spec.unrecoverableSignature)
    {
        web3.signatureR.assign(32, 0);
    }

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = web3.takeToTarsTransaction()]() mutable { return &inner; });
    tx->calculateHash(hasher);
    return tx;
}

/// Counts committedNonce() calls, so a test can state which plane a context consulted rather
/// than inferring it from a status code.
struct CountingWeb3NonceChecker : Web3NonceChecker
{
    using Web3NonceChecker::Web3NonceChecker;
    int reads = 0;
    task::Task<std::optional<u256>> committedNonce(std::string_view sender) override
    {
        ++reads;
        co_return co_await Web3NonceChecker::committedNonce(sender);
    }
};

/// The pending plane. Counting its reads is how the tests below say "this context did NOT do a
/// full account-state read": the balance is the half of that read the nonce path never touches.
struct CountingScheduler : FakeScheduler
{
    CountingScheduler() : FakeScheduler(nullptr, nullptr) {}
    int reads = 0;
    u256 balance;
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        ++reads;
        bcos::storage::Entry entry;
        entry.set(balance.convert_to<std::string>());
        co_return entry;
    }
};

/// A ledger nonce checker whose verdict the test dictates. checkNonce is virtual on
/// NonceCheckerInterface, so this needs no seam in the validator.
struct StubLedgerNonceChecker : LedgerNonceChecker
{
    explicit StubLedgerNonceChecker(TransactionStatus verdict)
      : LedgerNonceChecker({}, /*blockNumber=*/0, /*blockLimit=*/600,
            /*checkBlockLimit=*/false),
        m_verdict(verdict)
    {}
    TransactionStatus checkNonce(const Transaction&) override { return m_verdict; }
    TransactionStatus m_verdict;
};

/// Supplies the one field readAccountState leaves empty on every real path. Nothing populates
/// contract code at admission today (see readAccountState), so without this seam the EIP-3607
/// rule would be untestable and would silently rot until code becomes readable.
struct CodeInjectingValidator : TxValidator
{
    using TxValidator::TxValidator;
    bytes code;

protected:
    task::Task<std::optional<AccountState>> readAccountState(std::string_view sender) override
    {
        auto state = co_await TxValidator::readAccountState(sender);
        if (state)
        {
            state->code = code;
        }
        co_return state;
    }
};

/// Everything the validator needs, with knobs for the cases below.
///
/// Unlike the callback-injected version this replaces, the account state is SEEDED into the
/// fake ledger and read back through the validator's own path -- so the test exercises
/// readAccountState and Web3NonceChecker rather than a stand-in for them.
struct AdmitHarness
{
    std::shared_ptr<FakeLedger> ledger = std::make_shared<FakeLedger>();
    /// Edited by the case, never served directly. Null = publish nothing, leaving the holder in
    /// its construction-time empty state.
    ledger::LedgerConfig::Ptr ledgerConfig = std::make_shared<ledger::LedgerConfig>();
    ledger::LedgerConfigState::Ptr ledgerConfigState =
        std::make_shared<ledger::LedgerConfigState>();
    std::shared_ptr<CountingWeb3NonceChecker> web3NonceChecker;
    std::shared_ptr<CountingScheduler> scheduler = std::make_shared<CountingScheduler>();
    std::shared_ptr<LedgerNonceChecker> ledgerNonceChecker;
    /// Null by default: the mempool-side validator has no pool set, and most cases here are
    /// Web3, which never consult it.
    std::shared_ptr<NonceCheckerInterface> txPoolNonceChecker;
    /// Nothing is a system transaction unless a case says so.
    SystemTxPredicate isSystemTx = [](Transaction const&) { return false; };
    AccountState account{};
    bool accountExists = true;

    AdmitHarness()
    {
        // The snapshot is the only chain configuration admission reads. The fake ledger's
        // SYS_CONFIG map stays empty on purpose (see admissionReadsNoSystemConfig).
        ledgerConfig->setChainId(evmc::bytes32{kChainId});
        ledgerConfig->setGasPrice({"0", 0});  // free gas by default
        ledgerConfig->setBlockNumber(100);
        ledgerConfig->setGasLimit({3000000000ULL, 0});
        ledgerConfig->setEVMCRevision(EVMC_PRAGUE);
        web3NonceChecker = std::make_shared<CountingWeb3NonceChecker>(ledger);
        // Comfortably funded, nonce 7 (matches the fixture), plain EOA.
        account.balance = u256("0xffffffffffffffffffffffffffff");
        account.nonce = u256(7);
    }

    int accountStateReads() const { return scheduler->reads; }
    int accountNonceReads() const { return web3NonceChecker->reads; }

    /// The address the fixture key signs as. Committed state is keyed by it.
    static std::string senderHex()
    {
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        return toHex(senderKey().address(hashImpl));
    }

    /// Publish the account into the committed plane the validator actually reads.
    void publishAccount()
    {
        scheduler->balance = account.balance;
        if (!accountExists)
        {
            return;
        }
        ledger::StorageState state;
        state.nonce = account.nonce ? account.nonce->convert_to<std::string>() : "0";
        state.balance = account.balance.convert_to<std::string>();
        ledger->setStorageState(senderHex(), std::move(state));
    }

    /// Publish the case's chain configuration the way a committing node does: a copy, handed to
    /// the holder as const, so the object the validator reads is not the object the case edited.
    void publishChainConfig()
    {
        if (!ledgerConfig)
        {
            return;
        }
        ledgerConfigState->set(std::make_shared<ledger::LedgerConfig const>(*ledgerConfig));
    }

    std::unique_ptr<CodeInjectingValidator> make()
    {
        auto cryptoSuite =
            std::make_shared<crypto::CryptoSuite>(std::make_shared<crypto::Keccak256>(),
                std::make_shared<crypto::Secp256k1Crypto>(), nullptr);
        auto validator =
            std::make_unique<CodeInjectingValidator>(cryptoSuite, ledger, ledgerConfigState,
                txPoolNonceChecker, web3NonceChecker, isSystemTx, "group0", "chain0");
        validator->code = account.code;
        validator->setScheduler(scheduler);
        if (ledgerNonceChecker)
        {
            validator->setLedgerNonceChecker(ledgerNonceChecker);
        }
        return validator;
    }

    TransactionStatus run(Transaction& tx,
        AdmissionContext context = AdmissionContext::PoolAdmission,
        SignaturePolicy policy = SignaturePolicy::Required)
    {
        publishAccount();
        publishChainConfig();
        auto validator = make();
        return task::syncWait(validator->verify(tx, context, policy));
    }
};

}  // namespace bcos::test
