/**
 * @file Web3TxConsistencyWiringTest.cpp
 * @brief Prove TxValidator and TransactionSync actually call web3TarsFieldsMatchSignedExtra.
 */

#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-protocol/TransactionStatus.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;
using namespace bcos::sync;

namespace bcos::test
{
namespace
{
class TestableTransactionSync : public TransactionSync
{
public:
    using TransactionSync::TransactionSync;
    using TransactionSync::importDownloadedTxs;
};

protocol::Transaction::Ptr poisonAccessList(protocol::Transaction::Ptr tx)
{
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    BOOST_REQUIRE(impl);
    bcostars::Web3AccessListEntry forged;
    forged.account = "1111111111111111111111111111111111111111";
    impl->mutableInner().data.accessList.emplace_back(std::move(forged));
    return tx;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(Web3TxConsistencyWiringTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(txValidatorVerifyRejectsPoisonedTarsAccessList)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(keyPair->publicKey(), cryptoSuite,
        "group_web3_wiring_v", "chain_web3_wiring_v", 10, fakeGateWay, false, false);
    faker->init();

    auto eoaKey = signatureImpl->generateKeyPair();
    auto validator = faker->txpool()->txpoolConfig()->txValidator();

    auto poisoned = poisonAccessList(fakeWeb3Tx(cryptoSuite, "43", eoaKey));
    // Hits Web3TxConsistency before signature / nonce — proves TxValidator.cpp wiring.
    BOOST_CHECK_EQUAL(validator->verify(*poisoned), TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(transactionSyncPartialImportOnPoisonedBatch)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(keyPair->publicKey(), cryptoSuite,
        "group_web3_consistency", "chain_web3_consistency", 10, fakeGateWay, false, false);
    faker->init();

    auto eoaKey = signatureImpl->generateKeyPair();
    auto clean = fakeWeb3Tx(cryptoSuite, "100", eoaKey);
    auto poisoned = poisonAccessList(fakeWeb3Tx(cryptoSuite, "101", eoaKey));
    auto cleanHash = clean->hash();
    auto poisonedHash = poisoned->hash();
    BOOST_REQUIRE_NE(cleanHash, poisonedHash);

    auto txs = std::make_shared<Transactions>();
    txs->emplace_back(poisoned);
    txs->emplace_back(clean);

    auto proposal = faker->txpool()->txpoolConfig()->blockFactory()->createBlock();
    auto header =
        faker->txpool()->txpoolConfig()->blockFactory()->blockHeaderFactory()->createBlockHeader();
    header->setNumber(faker->ledger()->blockNumber() + 1);
    header->calculateHash(*hashImpl);
    proposal->setBlockHeader(header);

    auto sync = std::make_shared<TestableTransactionSync>(
        faker->sync()->config(), /*checkTransactionSignature=*/true);
    BOOST_CHECK(!sync->importDownloadedTxs(txs, proposal));
    BOOST_CHECK(poisoned->invalid());
    BOOST_CHECK(!clean->invalid());
    BOOST_CHECK(faker->txpool()->txpoolStorage()->exists(cleanHash));
    BOOST_CHECK(!faker->txpool()->txpoolStorage()->exists(poisonedHash));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
