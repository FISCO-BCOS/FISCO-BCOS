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
 * @file AdmitTest.cpp
 * @brief TxValidator::verify -- the checks that have no counterpart in the tree today.
 *
 * The module is stateless and every dependency is injected, so these run against fakes: no
 * transaction pool, no block sequence, no scheduler.
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/TxGasModel.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/testutils/faker/FakeScheduler.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-rlp-protocol/Web3TxEnvelope.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tx-validator/LedgerNonceChecker.h"
#include "bcos-tx-validator/Normalize.h"
#include "bcos-tx-validator/TxPoolNonceChecker.h"
#include "bcos-tx-validator/TxValidator.h"
#include "bcos-tx-validator/Web3NonceChecker.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txvalidator;

namespace bcos::test
{
namespace
{
constexpr uint64_t kChainId = 5;

/// One key pair for the whole suite, so `sender` is stable across cases.
crypto::Secp256k1Crypto& signer()
{
    static crypto::Secp256k1Crypto instance;
    return instance;
}
crypto::KeyPairInterface& senderKey()
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

std::shared_ptr<bcostars::protocol::TransactionImpl> admitTx(TxSpec const& spec = {})
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
    ledger::LedgerConfig::Ptr ledgerConfig = std::make_shared<ledger::LedgerConfig>();
    ledger::LedgerConfigState::Ptr ledgerConfigState;
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
        ledgerConfigState = std::make_shared<ledger::LedgerConfigState>(ledgerConfig);
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
        auto validator = make();
        return task::syncWait(validator->verify(tx, context, policy));
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(AdmitTest)

BOOST_AUTO_TEST_CASE(wellFormedTransactionIsAdmitted)
{
    AdmitHarness harness;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// ------------------------------------------------------------ checks with no counterpart today

// A gasLimit that cannot pay the base transaction cost is accepted today, sealed into a block,
// and fails deterministically in the executor with OutOfGasLimit.
BOOST_AUTO_TEST_CASE(gasLimitBelowIntrinsicCostIsRejected)
{
    // Accepted today, sealed into a block, then failed deterministically by the executor with
    // OutOfGasLimit -- a block carrying a transaction that cannot succeed.
    AdmitHarness harness;
    auto tx = admitTx({.gasLimit = 20000});  // below the 21000 base cost
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::OutOfGasLimit);
}

BOOST_AUTO_TEST_CASE(intrinsicGasBoundaryMatchesTheExecutorsCostModel)
{
    AdmitHarness harness;
    const auto calldata = bcos::bytes(100, 0x01);

    // Take the floor from the same function the executor uses rather than restating the
    // formula. Restating it is what the shared header exists to prevent -- and it would be
    // wrong here: with 100 non-zero bytes the intrinsic cost is 22600, but EIP-7623's minimum
    // (21000 + 400 tokens * 10) is 25000, and the transaction is charged the larger one.
    auto probe = admitTx({.gasLimit = 1000000, .data = calldata});
    const auto cost = protocol::gas::compute_tx_intrinsic_cost(EVMC_PRAGUE, *probe);
    const auto floor = static_cast<uint64_t>(std::max(cost.intrinsic, cost.min));
    BOOST_REQUIRE_GT(cost.min, cost.intrinsic);  // the EIP-7623 floor is what binds here

    auto atFloor = admitTx({.gasLimit = floor, .data = calldata});
    BOOST_CHECK(harness.run(*atFloor) == TransactionStatus::None);

    auto belowFloor = admitTx({.gasLimit = floor - 1, .data = calldata});
    BOOST_CHECK(harness.run(*belowFloor) == TransactionStatus::OutOfGasLimit);
}

// EIP-3860. The cap applies to contract CREATION only. The implementation this replaces keyed
// on transaction type alone, so a large call to an already-deployed contract was rejected as if
// its calldata were initcode.
BOOST_AUTO_TEST_CASE(oversizedInitCodeIsRejectedOnCreationOnly)
{
    AdmitHarness harness;
    const auto oversized = bcos::bytes(MAX_INITCODE_SIZE + 1, 0x01);

    auto creation = admitTx({.to = std::nullopt, .data = oversized});
    BOOST_CHECK(harness.run(*creation) == TransactionStatus::MaxInitCodeSizeExceeded);

    // The same payload as calldata to a deployed contract is not initcode. It still fails --
    // this gasLimit cannot pay for that much calldata -- but not with the initcode code, which
    // is the distinction under test.
    auto call = admitTx({.data = oversized});
    BOOST_CHECK(harness.run(*call) != TransactionStatus::MaxInitCodeSizeExceeded);
}

// EIP-3607. A contract address must not be able to originate a transaction.
BOOST_AUTO_TEST_CASE(contractSenderIsRejected)
{
    AdmitHarness harness;
    harness.account.code = {0x60, 0x60, 0x60};
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::SenderNoEOA);
}

// ... but EIP-7702 delegated code still belongs to an EOA.
BOOST_AUTO_TEST_CASE(delegatedSenderIsAccepted)
{
    AdmitHarness harness;
    harness.account.code = {0xef, 0x01, 0x00, 0xaa, 0xbb};
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// EIP-2681.
BOOST_AUTO_TEST_CASE(accountNonceAtMaxIsRejected)
{
    AdmitHarness harness;
    harness.account.nonce = u256(std::numeric_limits<uint64_t>::max());
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::NonceHasMaxValue);
}

BOOST_AUTO_TEST_CASE(alreadyUsedNonceIsRejected)
{
    AdmitHarness harness;
    harness.account.nonce = u256(12);  // fixture nonce is 7
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(nonceBeyondTheQueueWindowIsRejected)
{
    AdmitHarness harness;
    harness.account.nonce = u256(0);
    // Just inside the window: queued, not rejected. Admission keeps future-nonce transactions,
    // unlike execution, which requires an exact match.
    auto inside = admitTx({.nonce = DEFAULT_WEB3_NONCE_CHECK_LIMIT});
    BOOST_CHECK(harness.run(*inside) == TransactionStatus::None);

    auto beyond = admitTx({.nonce = DEFAULT_WEB3_NONCE_CHECK_LIMIT + 1});
    BOOST_CHECK(harness.run(*beyond) == TransactionStatus::NonceCheckFail);
}

// The chain's base fee. Today this is reported as InsufficientFunds, which points the user at
// their balance instead of at their fee cap.
BOOST_AUTO_TEST_CASE(feeCapBelowBaseFeeIsRejectedWithItsOwnCode)
{
    AdmitHarness harness;
    harness.ledgerConfig->setGasPrice({"0xfffffffffff", 0});
    auto tx = admitTx({.maxFeePerGas = 1000, .maxPriorityFeePerGas = 1});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::FeeCapLessThanBaseFee);
}

// A typed transaction on a chain whose revision predates its EIP.
BOOST_AUTO_TEST_CASE(typedTransactionBeforeItsForkIsRejected)
{
    AdmitHarness harness;
    harness.ledgerConfig->setEVMCRevision(EVMC_ISTANBUL);  // pre-Berlin, pre-London
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::TxTypeNotSupported);
}

BOOST_AUTO_TEST_CASE(gasLimitAboveTheChainCapIsRejected)
{
    AdmitHarness harness;
    harness.ledgerConfig->setGasLimit({50000, 0});
    auto tx = admitTx({.gasLimit = 60000});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::MaxGasLimitExceeded);
}

// ------------------------------------------------------------ balance, 512-bit

BOOST_AUTO_TEST_CASE(insufficientBalanceIsRejected)
{
    AdmitHarness harness;
    harness.ledgerConfig->setGasPrice({"1", 0});
    harness.account.balance = 1;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
}

// The overflow regression. gasLimit * effectiveGasPrice + value is computed at 512 bits because
// bcos::u256 carries boost::multiprecision::unchecked: at 256 bits the product of the fixture's
// gasLimit and a near-2^256 gas price wraps to a small number and an unfundable transaction is
// admitted. Asserted against the arithmetic directly -- the fixture's own gas price cannot be
// changed without re-signing it.
// The old validateBalance rejected on `balance < required || balance == 0`. The second clause
// is not carried over, and these two cases are why. On a free-gas chain it was unreachable --
// that implementation set skipBalanceCheck and returned before reaching it -- and on a
// gas-charging chain `balance < required` already covers a zero balance, because required is
// then value + gasLimit * gasPrice > 0. What is left is a zero-value transaction on a free-gas
// chain, which costs its sender nothing and which execution accepts.
BOOST_AUTO_TEST_CASE(zeroBalanceSenderMayStillSendAFreeZeroValueTransaction)
{
    AdmitHarness harness;  // tx_gas_price is "0" by default here
    harness.account.balance = 0;
    auto tx = admitTx({.value = 0});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// Free gas does not make `value` free: the old implementation skipped the whole balance check
// when gas was free, so an unfunded transfer was admitted and then failed in the executor.
BOOST_AUTO_TEST_CASE(freeGasStillRequiresTheValueToBeCovered)
{
    AdmitHarness harness;
    harness.account.balance = 0;
    auto tx = admitTx({.value = 5});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(feeArithmeticDoesNotWrapAt256Bits)
{
    // bcos::u256 carries boost::multiprecision::unchecked, so gasLimit * gasPrice is reduced mod
    // 2^256 with no signal. This drives a real transaction through checkBalance with a balance
    // sitting BETWEEN the wrapped product and the true one: a 256-bit implementation computes a
    // small requirement and admits it, a 512-bit one computes the real requirement and refuses.
    //
    // Asserting on the arithmetic alone (which an earlier version of this case did) passes
    // whatever checkBalance is implemented in, so it pins nothing.
    AdmitHarness harness;
    harness.ledgerConfig->setGasPrice({"0x1", 0});  // gas is charged

    constexpr uint64_t gasLimit = 100000;
    const auto gasPrice = std::numeric_limits<u256>::max();
    const u256 wrapped = u256(gasLimit) * gasPrice;
    BOOST_REQUIRE_MESSAGE(u512{wrapped} < u512{gasLimit} * u512{gasPrice},
        "the 256-bit product must be strictly smaller than the true one");

    // Clears the wrapped figure by one, nowhere near the real one.
    harness.account.balance = wrapped + 1;

    auto tx = admitTx(
        {.gasLimit = gasLimit, .maxFeePerGas = gasPrice, .maxPriorityFeePerGas = 0, .value = 0});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
}

// ------------------------------------------------------------ chainId

BOOST_AUTO_TEST_CASE(wrongChainIdIsRejected)
{
    AdmitHarness harness;
    harness.ledgerConfig->setChainId(evmc::bytes32{9999});
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InvalidChainId);
}

// Fail closed. Skipping the check when the chain has no web3_chain_id would accept transactions
// signed for any chain -- today EthEndpoint does exactly that. A holder nothing has published
// to is the one snapshot without a chain id (getLedgerConfig always sets one): the state between
// a node's construction and its first publish. It refuses rather than skips.
BOOST_AUTO_TEST_CASE(missingChainIdConfigRejectsRatherThanSkips)
{
    AdmitHarness harness;
    harness.ledgerConfigState = std::make_shared<ledger::LedgerConfigState>();
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InvalidChainId);
}

// The classifier answers three ways and the two cases above drive one of them. A pre-EIP-155
// legacy transaction makes no claim about which chain it is for -- its signing preimage is the
// bare six fields, with no chainId item to compare -- so the check stands down even though the
// chain HAS a configured id, which is what separates this from the case above.
BOOST_AUTO_TEST_CASE(unprotectedLegacyTransactionIsExemptFromTheChainIdCheck)
{
    AdmitHarness harness;
    auto tx = admitTx({.type = rpc::TransactionType::Legacy, .chainId = std::nullopt});
    BOOST_REQUIRE(harness.ledgerConfig->chainId().has_value());
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// The third answer: Malformed, which checkChainId refuses rather than granting it the exemption
// above. It never gets that far -- decoding rejects the same bytes first and normalization
// reports that as Malformed -- so what this pins is the ORDER, not checkChainId's Malformed arm.
// That arm is the second line of a defence whose first line is the decoder, kept because the
// walker and the decoder are only in sync as long as someone keeps them so (Web3TxEnvelope.h
// says exactly that). Should the order ever change, this case turns red and points at the arm
// that then becomes reachable. Blob and deposit envelopes below are pinned the same way.
//
// The mutation is a junk item, not an invalid `v`: a transaction built the way this fixture (and
// the RPC ingress) builds one carries the SIGNING PREIMAGE, with r/s/v in a separate tars field,
// because that is what Web3TarsBridge's encodeForSign produces. A peer chooses its own
// extraTransactionBytes and the block path carries sealed envelopes, so the classifier handles
// both forms; there is simply no v in THESE bytes to corrupt.
BOOST_AUTO_TEST_CASE(malformedEnvelopeIsRejectedBeforeTheChainIdCheck)
{
    AdmitHarness harness;
    auto tx = admitTx({.type = rpc::TransactionType::Legacy});
    auto& envelope = tx->mutableInner().extraTransactionBytes;
    // Short list header (0xc0..0xf7): one length byte, so appending one item is a bump plus a
    // push_back. The REQUIRE says so, rather than assuming it.
    const auto header = static_cast<uint8_t>(envelope.at(0));
    BOOST_REQUIRE(header >= 0xc0 && header < 0xf7);
    envelope.at(0) = static_cast<tars::Char>(header + 1);
    envelope.push_back(static_cast<tars::Char>(0x09));
    // Both halves of the claim: the classifier calls these bytes Malformed, and verify() still
    // answers with normalization's verdict rather than checkChainId's.
    BOOST_REQUIRE(rlp::protocol::classifyWeb3EnvelopeChainId(tx->extraTransactionBytes()).kind ==
                  rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::Malformed);
}

/// A ledger whose SYS_CONFIG reads fail outright.
struct SystemConfigUnreadableLedger : FakeLedger
{
    void asyncGetSystemConfigByKey(std::string_view const&,
        std::function<void(Error::Ptr, std::string, protocol::BlockNumber)>) override
    {
        throw std::runtime_error("SYS_CONFIG must not be read at admission");
    }
};

// The snapshot is the only source of chain configuration: the chain id and the base fee come
// from the LedgerConfig that getLedgerConfig built, not from a per-transaction getSystemConfig.
// Two storage round trips fewer on the hot path, and one source of truth instead of two -- this
// case is what keeps a later check from quietly bringing the second one back.
BOOST_AUTO_TEST_CASE(admissionReadsNoSystemConfig)
{
    AdmitHarness harness;
    harness.ledger = std::make_shared<SystemConfigUnreadableLedger>();
    harness.web3NonceChecker = std::make_shared<CountingWeb3NonceChecker>(harness.ledger);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// ------------------------------------------------------------ contexts and policy

// Proposal verification touches NO account state at all -- not balance, not code, not even the
// nonce. Counting the reads is the only way to state that as a test; a status assertion would
// pass just as well with every read still happening.
//
// This is stronger than it was: dropping Web3NonceWindow from the proposal column removed the
// last check that needed a nonce, so the consensus path now runs entirely on the transaction and
// the chain config. Anything that reintroduces a read here puts it on the hot path for every
// transaction of every block, and this case is what says so.
BOOST_AUTO_TEST_CASE(proposalVerificationReadsNoAccountState)
{
    AdmitHarness harness;
    auto tx = admitTx();
    BOOST_CHECK(
        harness.run(*tx, AdmissionContext::ProposalVerification) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);
    BOOST_CHECK_EQUAL(harness.accountNonceReads(), 0);
}

// The vulnerability F2 names: nothing downstream re-checks tx.chainId (EthereumTransition.h
// says validate_transaction does not look at that field), so if proposal verification skips it
// a leader can have every follower execute a transaction signed for another chain.
BOOST_AUTO_TEST_CASE(proposalVerificationRejectsAForeignChainId)
{
    AdmitHarness harness;
    harness.ledgerConfig->setChainId(evmc::bytes32{9999});
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::ProposalVerification) ==
                TransactionStatus::InvalidChainId);
}

// Protocol invariants are enforced on the proposal column too -- they cost no account read and
// their answer is the same on every honest node.
BOOST_AUTO_TEST_CASE(proposalVerificationRejectsUnderIntrinsicGas)
{
    AdmitHarness harness;
    auto tx = admitTx({.gasLimit = 20000});
    BOOST_CHECK(harness.run(*tx, AdmissionContext::ProposalVerification) ==
                TransactionStatus::OutOfGasLimit);
}

BOOST_AUTO_TEST_CASE(poolAdmissionReadsTheFullAccountState)
{
    AdmitHarness harness;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
    BOOST_CHECK_GT(harness.accountStateReads(), 0);
}

// Proposal verification skips the checks whose inputs are local state, so a transaction the
// local node would refuse to admit does not fail the whole proposal.
BOOST_AUTO_TEST_CASE(proposalVerificationIgnoresLocalBalance)
{
    AdmitHarness harness;
    harness.ledgerConfig->setGasPrice({"1", 0});
    harness.account.balance = 0;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);

    auto same = admitTx();
    BOOST_CHECK(
        harness.run(*same, AdmissionContext::ProposalVerification) == TransactionStatus::None);
}

// EEST fixtures use arbitrary nonces and unfunded accounts.
BOOST_AUTO_TEST_CASE(eestReplaySkipsBalanceAndNonceWindowOnly)
{
    AdmitHarness harness;
    harness.ledgerConfig->setGasPrice({"1", 0});
    harness.account.balance = 0;
    harness.account.nonce = u256(999999);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::EESTReplay) == TransactionStatus::None);

    // The chainId check is NOT relaxed.
    harness.ledgerConfig->setChainId(evmc::bytes32{9999});
    auto wrongChain = admitTx();
    BOOST_CHECK(harness.run(*wrongChain, AdmissionContext::EESTReplay) ==
                TransactionStatus::InvalidChainId);
}

// SignaturePolicy::Disabled drops the sender-dependent checks explicitly, instead of relying on
// the sender happening to be empty -- which today makes every transaction look like it has a
// zero balance.
BOOST_AUTO_TEST_CASE(disabledSignaturePolicySkipsSenderDependentChecks)
{
    AdmitHarness harness;
    harness.ledgerConfig->setGasPrice({"1", 0});
    harness.account.balance = 0;
    harness.account.nonce = u256(999999);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);

    // Checks that need no sender still run.
    harness.ledgerConfig->setChainId(evmc::bytes32{9999});
    auto wrongChain = admitTx();
    BOOST_CHECK(harness.run(*wrongChain, AdmissionContext::PoolAdmission,
                    SignaturePolicy::Disabled) == TransactionStatus::InvalidChainId);
}

// Web3PoolNonce is sender-dependent for a reason worth its own case: its key is the PAIR
// (sender, nonce), where BcosPoolNonce's is a nonce value alone. Run with no recovered sender it
// would file every pending Web3 transaction under the empty string, and one sender's nonce would
// then refuse another's.
//
// The reservation here is the one such an admission would have written: nonce 7 under the empty
// sender. A transaction with nonce 7 from a real sender must still be admitted.
BOOST_AUTO_TEST_CASE(disabledSignaturePolicySkipsTheWeb3PendingNonceCheck)
{
    AdmitHarness harness;
    BOOST_REQUIRE(task::syncWait(harness.web3NonceChecker->insertMemoryNonce("", "7")));
    auto tx = admitTx({.nonce = 7});
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::None);
}

// The P0 regression. This transaction is built exactly the way EthEndpoint::sendRawTransaction
// builds one -- constructed directly, so tainted() is true. An implementation that read
// tainted() as "already verified" would admit it despite the broken signature.
BOOST_AUTO_TEST_CASE(taintedTransactionWithABrokenSignatureIsStillRejected)
{
    AdmitHarness harness;
    auto tx = admitTx();
    BOOST_REQUIRE(tx->tainted());
    tx->mutableInner().signature[0] = static_cast<tars::Char>(tx->inner().signature[0] ^ 0x01);
    // The hash commits to the signature, so normalization catches it first; either way it must
    // not be admitted.
    BOOST_CHECK(harness.run(*tx) != TransactionStatus::None);
}

// ------------------------------------------------------------ stages

// The gate stage reads nothing. A transaction refused there -- here by an unrecoverable
// signature, the case a P2P peer can manufacture for free -- costs no account read, which is what
// keeps unauthenticated input cheap to refuse. Pinned by counting, as the proposal case above
// is: a status assertion would pass just as well with the reads still happening.
BOOST_AUTO_TEST_CASE(aRejectionAtTheGateReadsNoAccountState)
{
    AdmitHarness harness;
    auto tx = admitTx({.unrecoverableSignature = true});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InvalidSignature);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);
    BOOST_CHECK_EQUAL(harness.accountNonceReads(), 0);
}

// The system-transaction mark is a property of the transaction (its `to` is a system contract),
// not of any check. The pool-side validator set it inside its signature path, which
// SignaturePolicy::Disabled would switch off along with the signature; it is set before the
// stages run, so the policy cannot reach it.
BOOST_AUTO_TEST_CASE(systemTransactionIsMarkedRegardlessOfSignaturePolicy)
{
    AdmitHarness harness;
    harness.isSystemTx = [](Transaction const&) { return true; };
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x0000000000000000000000000000000000001000";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    BOOST_REQUIRE(!tx->systemTx());

    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::None);
    BOOST_CHECK(tx->systemTx());
}

// ------------------------------------------------------------ every kind, through verify()

// The routing table names five kinds and the cases above drive one of them. These drive the rest,
// so that each kind's own checks are exercised by verify() and not pinned on the table alone.
BOOST_AUTO_TEST_CASE(legacyTransactionIsAdmitted)
{
    AdmitHarness harness;
    auto tx = admitTx({.type = rpc::TransactionType::Legacy});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(accessListTransactionIsAdmitted)
{
    AdmitHarness harness;
    auto tx = admitTx({.type = rpc::TransactionType::EIP2930});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(setCodeTransactionIsAdmitted)
{
    AdmitHarness harness;
    auto tx = admitTx({.type = rpc::TransactionType::EIP7702, .withAuthorization = true});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// EIP-7702: a set-code transaction cannot create a contract, and cannot carry nothing to set.
BOOST_AUTO_TEST_CASE(setCodeTransactionWithoutRecipientIsRejected)
{
    AdmitHarness harness;
    auto tx = admitTx(
        {.type = rpc::TransactionType::EIP7702, .to = std::nullopt, .withAuthorization = true});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::CreateSetCodeTx);
}

BOOST_AUTO_TEST_CASE(setCodeTransactionWithEmptyAuthorizationListIsRejected)
{
    AdmitHarness harness;
    auto tx = admitTx({.type = rpc::TransactionType::EIP7702});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::EmptyAuthorizationList);
}

BOOST_AUTO_TEST_CASE(priorityFeeAboveTheFeeCapIsRejected)
{
    AdmitHarness harness;
    auto tx = admitTx({.maxFeePerGas = 1000, .maxPriorityFeePerGas = 2000});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::TipGreaterThanFeeCap);
}

// A blob or deposit envelope is refused before the table is consulted: normalization classifies
// the envelope and rejects both, so Check::TypeGate's Rejected arm is the second line, not the
// first. What verify() answers for them is pinned here, whichever line answered.
BOOST_AUTO_TEST_CASE(blobAndDepositEnvelopesAreRefused)
{
    AdmitHarness harness;
    auto blob = admitTx();
    blob->mutableInner().extraTransactionBytes.at(0) = 0x03;
    BOOST_CHECK(harness.run(*blob) == TransactionStatus::BlobTxNotAllowed);

    auto deposit = admitTx();
    deposit->mutableInner().extraTransactionBytes.at(0) = 0x7e;
    BOOST_CHECK(harness.run(*deposit) == TransactionStatus::TxTypeNotSupported);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);
}

// The gate's `to` format check, on the only kind whose `to` is a free-form string: a Web3 `to`
// is decoded from the envelope and is 20 bytes or empty by construction.
BOOST_AUTO_TEST_CASE(malformedRecipientIsRejectedAtTheGate)
{
    AdmitHarness harness;
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x12";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::Malformed);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);
}

// EIP-2. Every ECDSA signature has a twin with s' = n - s over the same message, and recovery
// succeeds on it -- to a different address. Without the low-s rule the same envelope enters the
// pool a second time, under a second sender and a second hash. This is the only place a
// tars-form Web3 transaction from a peer meets the rule: the raw-bytes decode on the RPC
// ingress is not on that path. Without the rule this case reaches the balance check as the
// twin's sender and fails there instead.
BOOST_AUTO_TEST_CASE(highSSignatureIsRejected)
{
    AdmitHarness harness;
    auto tx = admitTx({.highS = true});
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InvalidSignature);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);
}

// ------------------------------------------------------------ wiring guard

// Two of the constructor's arguments are mandatory and the rest are not, so the difference is
// pinned here. A null snapshot holder would turn every revision-dependent check into a
// stand-down; a null Web3 nonce checker would be dereferenced by the first Web3 transaction's
// account read. Neither can be answered with a transaction status, so neither is allowed to be
// built.
BOOST_AUTO_TEST_CASE(mandatoryCollaboratorsAreRefusedAtConstruction)
{
    AdmitHarness harness;
    auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(std::make_shared<crypto::Keccak256>(),
        std::make_shared<crypto::Secp256k1Crypto>(), nullptr);
    BOOST_CHECK_THROW(TxValidator(cryptoSuite, harness.ledger, nullptr, nullptr,
                          harness.web3NonceChecker, harness.isSystemTx, "group0", "chain0"),
        std::invalid_argument);
    BOOST_CHECK_THROW(TxValidator(cryptoSuite, harness.ledger, harness.ledgerConfigState, nullptr,
                          nullptr, harness.isSystemTx, "group0", "chain0"),
        std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(bcosLedgerNonceWithoutABoundCheckerPasses)
{
    AdmitHarness harness;
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    // The ledger nonce checker is bound only once the pool has read the chain's block limit.
    // Before that there is no window to judge against, and PASSING is the correct answer:
    // execution still enforces the nonce, whereas rejecting would refuse every transaction
    // submitted during startup.
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(bcosLedgerNonceVerdictIsPropagated)
{
    AdmitHarness harness;
    harness.ledgerNonceChecker =
        std::make_shared<StubLedgerNonceChecker>(TransactionStatus::BlockLimitCheckFail);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";

    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::BlockLimitCheckFail);
}

// The pending-nonce set is this node's own pool: a leader's transactions are legitimately absent
// from it, and two honest nodes' pools differ by what each has admitted and not yet sealed. So
// it decides admission but not proposal verification -- the pool-side validator this module
// replaces made the same call with checkTransaction(tx, onlyCheckLedgerNonce = true). The
// routing table now says it (BcosPoolNonce is off the proposal column) and this case pins it
// end to end, with the committed-nonce half still enforced on both paths.
BOOST_AUTO_TEST_CASE(bcosPendingNonceRejectsAdmissionButNotProposals)
{
    AdmitHarness harness;
    auto pool = std::make_shared<TxPoolNonceChecker>();
    pool->insert("pending-nonce");
    harness.txPoolNonceChecker = pool;
    harness.ledgerNonceChecker = std::make_shared<StubLedgerNonceChecker>(TransactionStatus::None);

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    tx->mutableInner().data.nonce = "pending-nonce";

    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::NonceCheckFail);
    BOOST_CHECK(harness.run(*tx, AdmissionContext::ProposalVerification,
                    SignaturePolicy::Disabled) == TransactionStatus::None);

    // ... and the committed-nonce half is not what let the proposal through: a ledger verdict
    // still fails it.
    harness.ledgerNonceChecker =
        std::make_shared<StubLedgerNonceChecker>(TransactionStatus::NonceCheckFail);
    BOOST_CHECK(harness.run(*tx, AdmissionContext::ProposalVerification,
                    SignaturePolicy::Disabled) == TransactionStatus::NonceCheckFail);
}

// The Web3 counterpart, and the rule this module dropped when it stopped calling the pool-side
// validator's checkWeb3Nonce: a (sender, nonce) pair a pending transaction already holds is
// refused at admission, and ignored on the proposal path where two honest nodes' pools differ.
//
// MemoryStorage reserves the pair with insertMemoryNonce once verify() has passed, and THAT is
// what refuses two concurrent submissions of one pair -- one atomic step, no window. What this
// pins is that the answer is also given here, by a rule the table names, rather than only inside
// that storage call.
BOOST_AUTO_TEST_CASE(web3PendingNonceRejectsAdmissionButNotProposals)
{
    AdmitHarness harness;
    auto first = admitTx();
    BOOST_REQUIRE(harness.run(*first) == TransactionStatus::None);
    // Exactly the call MemoryStorage makes after a successful admission.
    BOOST_REQUIRE(task::syncWait(harness.web3NonceChecker->insertMemoryNonce(
        std::string(first->sender()), std::string(first->nonce()))));

    auto second = admitTx();
    BOOST_CHECK(harness.run(*second) == TransactionStatus::NonceCheckFail);
    BOOST_CHECK(
        harness.run(*second, AdmissionContext::ProposalVerification) == TransactionStatus::None);
}

// A BCOS transaction's hash is NOT recomputed from its data when the wire already carries one:
// TarsHashable.h returns the supplied dataHash verbatim. So signature recovery runs against
// whatever hash the sender put on the wire, and binds the recovered address to data that hash
// does not cover.
//
// Concretely: take any (hash, signature) pair from a victim's broadcast transaction, attach it to
// a body of your own choosing, and admission recovers the victim as the sender. The pool-side
// validator this module replaces defended against exactly this by calling clearSenderAndHash()
// before verify(), which wipes the wire hash and forces recomputation from `data`.
BOOST_AUTO_TEST_CASE(forgedBcosDataHashDoesNotBindTheVictimAsSender)
{
    AdmitHarness harness;
    auto hashImpl = std::make_shared<crypto::Keccak256>();
    auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();

    // A legitimate transaction the victim broadcast. Its hash and signature are public.
    bcostars::Transaction victimInner;
    victimInner.data.to = "0x1111111111111111111111111111111111111111";
    victimInner.data.nonce = "1";
    victimInner.data.groupID = "group0";
    victimInner.data.chainID = "chain0";
    auto victim = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = std::move(victimInner)]() mutable { return &inner; });
    victim->calculateHash(*hashImpl);
    auto signature = signatureImpl->sign(senderKey(), victim->hash(), true);
    victim->setSignatureData(*signature);
    auto const stolenHash = victim->hash();

    // The attacker's body, carrying the victim's hash and signature.
    bcostars::Transaction forgedInner;
    forgedInner.data.to = "0x2222222222222222222222222222222222222222";
    forgedInner.data.nonce = "2";
    forgedInner.data.groupID = "group0";
    forgedInner.data.chainID = "chain0";
    forgedInner.dataHash.assign(stolenHash.begin(), stolenHash.end());
    forgedInner.signature.assign(signature->begin(), signature->end());
    auto forged = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = std::move(forgedInner)]() mutable { return &inner; });

    harness.run(*forged, AdmissionContext::PoolAdmission, SignaturePolicy::Required);

    // What the victim's key actually signed.
    auto const [ok, victimAddress] =
        signatureImpl->recoverAddress(*hashImpl, stolenHash, ref(*signature));
    BOOST_REQUIRE(ok);

    // The forged body must NOT be attributed to the victim. Admission may still accept it as
    // some other (meaningless) sender -- what must never happen is the victim's address ending
    // up on a body the victim never signed.
    std::string_view const victimView(
        reinterpret_cast<char const*>(victimAddress.data()), victimAddress.size());
    BOOST_CHECK(forged->sender() != victimView);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
