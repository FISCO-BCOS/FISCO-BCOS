/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
// enforceSubmitTransaction reports whatever verdict proposal verification reached, and
// batchVerifyAndSubmitTransaction refuses the proposal on any of them. Before #5535 wired the
// validator into this path it acted on NonceCheckFail from checkTransaction alone (base
// MemoryStorage.cpp:293-294): a failed block limit was sealed anyway, and the chain id and the
// intrinsic-gas floor were not asked at all. AdmitTest pins that verify() answers InvalidChainId
// and OutOfGasLimit under ProposalVerification; this pins that the storage acts on the answer, at
// the call site a regression back to `== NonceCheckFail` would reopen. Negative control: with that
// comparison restored, both refusal cases here accept.

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-txpool/txpool/utilities/SystemTransaction.h"
#include "bcos-utilities/IOServicePool.h"
#include <bcos-tx-validator/TxPoolNonceChecker.h>
#include <bcos-tx-validator/TxValidator.h>
#include <bcos-tx-validator/Web3NonceChecker.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
constexpr uint64_t c_chainId = 5;
constexpr uint64_t c_enoughGas = 100000;

struct ProposalRejectFixture
{
    ProposalRejectFixture()
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(100);
        ledgerConfig->setGasLimit({3000000000ULL, 0});
        ledgerConfig->setGasPrice({"0", 0});
        ledgerConfig->setChainId(evmc::bytes32{c_chainId});
        // A revision is what makes IntrinsicGas judge at all: the floor is revision-dependent.
        ledgerConfig->setEVMCRevision(EVMC_PRAGUE);
        auto configState = std::make_shared<ledger::LedgerConfigState>(std::move(ledgerConfig));

        auto ledger = std::make_shared<FakeLedger>();
        auto txPoolNonceChecker = std::make_shared<txvalidator::TxPoolNonceChecker>();
        auto web3NonceChecker = std::make_shared<txvalidator::Web3NonceChecker>(ledger);
        auto validator = std::make_shared<txvalidator::TxValidator>(cryptoSuite, ledger,
            configState, txPoolNonceChecker, web3NonceChecker, &isSystemTransaction, "group_test",
            "chain_test");
        auto config = std::make_shared<TxPoolConfig>(validator, nullptr, nullptr, ledger,
            txPoolNonceChecker, web3NonceChecker, /*blockLimit*/ 1000, /*poolLimit*/ 1024,
            /*checkSig*/ true);
        storage = std::make_unique<MemoryStorage>(config, *ioServicePool->getIOService());
    }

    /// One proposal transaction signed for @p chainId with @p gasLimit, through the path a
    /// leader's proposal takes when the transaction is missing from the local pool.
    bool proposalAccepted(uint64_t chainId, uint64_t gasLimit)
    {
        auto txs = std::make_shared<Transactions>();
        txs->emplace_back(fakeWeb3Tx(cryptoSuite, "7", key, "proposal", gasLimit, chainId));
        lastHash = txs->front()->hash();
        return storage->batchVerifyAndSubmitTransaction(nullptr, txs);
    }

    CryptoSuite::Ptr cryptoSuite = std::make_shared<CryptoSuite>(
        std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr);
    KeyPairInterface::UniquePtr key = cryptoSuite->signatureImpl()->generateKeyPair();
    IOServicePool::Ptr ioServicePool = std::make_shared<IOServicePool>(1, "proposalReject");
    std::unique_ptr<MemoryStorage> storage;
    HashType lastHash;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(ProposalRejectStatusTest, ProposalRejectFixture)

// The control: the same builder with the chain's own id and enough gas is sealed, so a refusal
// below is the status under test and not the fixture.
BOOST_AUTO_TEST_CASE(proposalWithTheChainsIdAndEnoughGasIsAccepted)
{
    BOOST_CHECK(proposalAccepted(c_chainId, c_enoughGas));
    BOOST_CHECK(storage->exists(lastHash));
}

BOOST_AUTO_TEST_CASE(proposalCarryingAForeignChainIdIsRefused)
{
    BOOST_CHECK(!proposalAccepted(c_chainId + 1, c_enoughGas));
    BOOST_CHECK(!storage->exists(lastHash));
}

BOOST_AUTO_TEST_CASE(proposalCarryingUnderIntrinsicGasIsRefused)
{
    BOOST_CHECK(!proposalAccepted(c_chainId, 20000));  // below the 21000 base cost
    BOOST_CHECK(!storage->exists(lastHash));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
