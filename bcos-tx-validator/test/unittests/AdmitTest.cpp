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
 * @brief TxValidator::admit -- the checks that have no counterpart in the tree today.
 *
 * The module is stateless and every dependency is injected, so these run against fakes: no
 * transaction pool, no block sequence, no scheduler.
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/TxGasModel.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tx-validator/Normalize.h"
#include "bcos-tx-validator/TxValidator.h"
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

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = web3.takeToTarsTransaction()]() mutable { return &inner; });
    tx->calculateHash(hasher);
    return tx;
}

/// Everything the validator needs, with knobs for the cases below.
struct AdmitHarness
{
    std::shared_ptr<FakeLedger> ledger = std::make_shared<FakeLedger>();
    ledger::LedgerConfig::Ptr ledgerConfig = std::make_shared<ledger::LedgerConfig>();
    AccountState account{};
    bool accountExists = true;
    int accountStateReads = 0;
    int accountNonceReads = 0;

    AdmitHarness()
    {
        ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, std::to_string(kChainId));
        ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0");  // free gas by default
        ledgerConfig->setBlockNumber(100);
        ledgerConfig->setGasLimit({3000000000ULL, 0});
        ledgerConfig->setEVMCRevision(EVMC_PRAGUE);
        // Comfortably funded, nonce 7 (matches the fixture), plain EOA.
        account.balance = u256("0xffffffffffffffffffffffffffff");
        account.nonce = u256(7);
    }

    TxValidator make()
    {
        auto cryptoSuite =
            std::make_shared<crypto::CryptoSuite>(std::make_shared<crypto::Keccak256>(),
                std::make_shared<crypto::Secp256k1Crypto>(), nullptr);
        return TxValidator{cryptoSuite, ledger,
            [this]() -> task::Task<ledger::LedgerConfig::Ptr> { co_return ledgerConfig; },
            [this](std::string_view) -> task::Task<std::optional<AccountState>> {
                ++accountStateReads;
                co_return accountExists ? std::make_optional(account) : std::nullopt;
            },
            [this](std::string_view) -> task::Task<std::optional<u256>> {
                ++accountNonceReads;
                co_return accountExists ? account.nonce : std::optional<u256>{};
            },
            [](Transaction const&) { return false; }, "group0", "chain0"};
    }

    TransactionStatus run(Transaction& tx,
        AdmissionContext context = AdmissionContext::PoolAdmission,
        SignaturePolicy policy = SignaturePolicy::Required, PoolNonceQuery pool = {})
    {
        auto validator = make();
        return task::syncWait(validator.admit(tx, context, policy, pool));
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
    const auto cost = protocol::compute_tx_intrinsic_cost(EVMC_PRAGUE, *probe);
    const auto floor = static_cast<uint64_t>(std::max(cost.intrinsic, cost.min));
    BOOST_REQUIRE_GT(cost.min, cost.intrinsic);  // the EIP-7623 floor is what binds here

    auto atFloor = admitTx({.gasLimit = floor, .data = calldata});
    BOOST_CHECK(harness.run(*atFloor) == TransactionStatus::None);

    auto belowFloor = admitTx({.gasLimit = floor - 1, .data = calldata});
    BOOST_CHECK(harness.run(*belowFloor) == TransactionStatus::OutOfGasLimit);
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
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0xfffffffffff");
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
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "1");
    harness.account.balance = 1;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
}

// The overflow regression. gasLimit * effectiveGasPrice + value is computed at 512 bits because
// bcos::u256 carries boost::multiprecision::unchecked: at 256 bits the product of the fixture's
// gasLimit and a near-2^256 gas price wraps to a small number and an unfundable transaction is
// admitted. Asserted against the arithmetic directly -- the fixture's own gas price cannot be
// changed without re-signing it.
BOOST_AUTO_TEST_CASE(feeArithmeticDoesNotWrapAt256Bits)
{
    // bcos::u256 carries boost::multiprecision::unchecked, so gasLimit * gasPrice is reduced
    // mod 2^256 with no signal. The 512-bit product is the real cost; a balance sitting between
    // the two is exactly the case where a 256-bit check admits an unfundable transaction.
    constexpr uint64_t gasLimit = 100000;
    const auto gasPrice = std::numeric_limits<u256>::max();

    const u256 wrapped = u256(gasLimit) * gasPrice;
    const u512 wide = u512{gasLimit} * u512{gasPrice};

    BOOST_REQUIRE_MESSAGE(
        u512{wrapped} < wide, "the 256-bit product must be strictly smaller than the true one");

    // A balance that clears the wrapped figure does not clear the real one.
    const u512 balance = u512{wrapped} + 1;
    BOOST_CHECK(balance < wide);
}

// ------------------------------------------------------------ chainId

BOOST_AUTO_TEST_CASE(wrongChainIdIsRejected)
{
    AdmitHarness harness;
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "9999");
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InvalidChainId);
}

// Fail closed. Skipping the check when the chain has no web3_chain_id would accept transactions
// signed for any chain -- today EthEndpoint does exactly that.
BOOST_AUTO_TEST_CASE(missingChainIdConfigRejectsRatherThanSkips)
{
    AdmitHarness harness;
    harness.ledger = std::make_shared<FakeLedger>();
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0");
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InvalidChainId);
}

// ------------------------------------------------------------ contexts and policy

// ADR-0011: proposal verification must not read balance or contract code it never looks at. A
// reader that counts calls is the only way to state that as a test.
BOOST_AUTO_TEST_CASE(proposalVerificationReadsOnlyTheNonce)
{
    AdmitHarness harness;
    auto tx = admitTx();
    BOOST_CHECK(
        harness.run(*tx, AdmissionContext::ProposalVerification) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.accountStateReads, 0);
    BOOST_CHECK_GT(harness.accountNonceReads, 0);
}

BOOST_AUTO_TEST_CASE(poolAdmissionReadsTheFullAccountState)
{
    AdmitHarness harness;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
    BOOST_CHECK_GT(harness.accountStateReads, 0);
}

// Proposal verification skips the checks whose inputs are local state, so a transaction the
// local node would refuse to admit does not fail the whole proposal.
BOOST_AUTO_TEST_CASE(proposalVerificationIgnoresLocalBalance)
{
    AdmitHarness harness;
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "1");
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
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "1");
    harness.account.balance = 0;
    harness.account.nonce = u256(999999);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::EESTReplay) == TransactionStatus::None);

    // The chainId check is NOT relaxed.
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "9999");
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
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "1");
    harness.account.balance = 0;
    harness.account.nonce = u256(999999);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.accountStateReads, 0);

    // Checks that need no sender still run.
    harness.ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "9999");
    auto wrongChain = admitTx();
    BOOST_CHECK(harness.run(*wrongChain, AdmissionContext::PoolAdmission,
                    SignaturePolicy::Disabled) == TransactionStatus::InvalidChainId);
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

// ------------------------------------------------------------ wiring guard

BOOST_AUTO_TEST_CASE(bcosPoolNonceWithoutAWiredQueryReportsUnknown)
{
    AdmitHarness harness;
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    // No PoolNonceQuery wired: a configuration error, not a defect in the transaction.
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::Unknown);
}

BOOST_AUTO_TEST_CASE(bcosPoolNonceQueryResultIsPropagated)
{
    AdmitHarness harness;
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";

    PoolNonceQuery pool{.checkBcosNonce = [](Transaction const&, bool) {
        return TransactionStatus::BlockLimitCheckFail;
    }};
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled,
                    pool) == TransactionStatus::BlockLimitCheckFail);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
