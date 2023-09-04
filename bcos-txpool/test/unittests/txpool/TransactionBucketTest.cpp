#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-tars-protocol/testutil/FakeTransaction.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <exception>
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{


BOOST_FIXTURE_TEST_SUITE(TestTransactionBucket, TestPromptFixture)

Transactions importTransactions(
    size_t _txsNum, const CryptoSuite::Ptr& _cryptoSuite, const TxPoolFixture::Ptr& _faker)
{
    auto txpool = _faker->txpool();
    auto ledger = _faker->ledger();
    Transactions transactions;
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto transaction = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 1000 + i),
            ledger->blockNumber() + 1, _faker->chainId(), _faker->groupId());
        transactions.push_back(transaction);
    }
    auto startT = utcTime();
    while (txpool->txpoolStorage()->size() < _txsNum && (utcTime() - startT <= 10000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return transactions;
}


void testTransactionBucket()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();

    std::string groupId = "test-group";
    std::string chainId = "test-chain";
    int64_t blockLimit = 10;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto fakerTxPool = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, fakeGateWay);

    // init the config
    fakerTxPool->init();

    // check the txpool config
    auto txpoolConfig = fakerTxPool->txpool()->txpoolConfig();
    BOOST_CHECK(txpoolConfig->txPoolNonceChecker());
    BOOST_CHECK(txpoolConfig->txValidator());
    BOOST_CHECK(txpoolConfig->blockFactory());
    BOOST_CHECK(txpoolConfig->txFactory());
    BOOST_CHECK(txpoolConfig->ledger());

    auto txpool = fakerTxPool->txpool();
    auto ledger = fakerTxPool->ledger();
    auto txpoolStorage = txpool->txpoolStorage();
    //    auto transactions1 = importTransactions(10, cryptoSuite, fakerTxPool);
    //    txpoolStorage->batchInsert(transactions1);
    //    std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;

    size_t txsNum = 20;
    Transactions transactions2;
    for (size_t i = 0; i < txsNum; i++)
    {
        auto transaction = fakeTransaction(cryptoSuite, std::to_string(utcTime() + 1000 + i),
            ledger->blockNumber() + 1, fakerTxPool->chainId(), fakerTxPool->groupId());
        transactions2.push_back(transaction);
        task::wait(txpoolStorage->submitTransaction(transaction));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;
    BOOST_CHECK(txpoolStorage->size() == txsNum);

    auto fetchedTxs = txpoolStorage->fetchNewTxs(txsNum);
    std::vector<std::pair<bcos::crypto::HashType, int64_t>> txsTimeStamp;
    size_t count = 0;
    for (const auto& tx : *fetchedTxs)
    {
        txsTimeStamp.emplace_back(tx->hash(), tx->importTime());
        std::cout << "#### test ####" << txsTimeStamp.size() << "#### txhash: " << tx->hash()
                  << "#### timeStamp: " << tx->importTime() << std::endl;
    }
    //    std::sort(txsTimeStamp.begin(), txsTimeStamp.end(), []
    //        (const std::pair<bcos::crypto::HashType, int64_t>& a, const
    //        std::pair<bcos::crypto::HashType, int64_t>& b) { return a.second < b.second;
    //    });
    BOOST_CHECK(txsTimeStamp.size() == txsNum);

    // sealTxs by timeStamp
    bool finish = false;
    HashListPtr sealedTxHashes = std::make_shared<crypto::HashList>();
    txpool->asyncSealTxs(txsNum, nullptr,
        [&](const Error::Ptr& error, const Block::Ptr& fetchedTxs, const Block::Ptr&) {
            BOOST_CHECK(error == nullptr);
            for (size_t i = 0; i < fetchedTxs->transactionsMetaDataSize(); i++)
            {
                sealedTxHashes->emplace_back(fetchedTxs->transactionHash(i));
                std::cout << "#### test #### " << i
                          << "#### sealedTxHash: " << fetchedTxs->transactionHash(i) << std::endl;
            }
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    for (size_t i = 0; i < sealedTxHashes->size(); ++i)
    {
        std::cout << "#### test ####" << i << " #### txsTimeStamp txhash: " << txsTimeStamp[i].first
                  << " ####sealedTxHashes: " << sealedTxHashes->at(i).hex() << std::endl;
        // BOOST_CHECK(txsTimeStamp[i].first == sealedTxHashes->at(i));
    }

    //    finish = false;
    //    HashListPtr sealedTxHashes1 = std::make_shared<crypto::HashList>();
    //    txpool->asyncSealTxs(txsNum, nullptr,
    //        [&](const Error::Ptr& error, const Block::Ptr& fetchedTxs, const Block::Ptr&) {
    //            BOOST_CHECK(error == nullptr);
    //            for (size_t i = 0; i < fetchedTxs->transactionsMetaDataSize(); i++)
    //            {
    //                sealedTxHashes1->emplace_back(fetchedTxs->transactionHash(i));
    //                std::cout << "#### test #### " << i
    //                          << "#### sealedTxHash: " << fetchedTxs->transactionHash(i) <<
    //                          std::endl;
    //            }
    //            finish = true;
    //        });
    //    while (!finish)
    //    {
    //        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    //    }
    //    for (size_t i = 0; i < txsNum / 2; ++i)
    //    {
    //        std::cout << "#### test ####" << i
    //                  << "#### txsTimeStamp txhash: " << txsTimeStamp[txsNum / 2 + i].first
    //                  << "####sealedTxHashes: " << sealedTxHashes1->at(i).hex() << std::endl;
    //        BOOST_CHECK(txsTimeStamp[txsNum / 2 + i].first == sealedTxHashes1->at(i));
    //    }


    // clear txpoolStorage txs
    txpoolStorage->clear();
    std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;
    BOOST_CHECK(txpoolStorage->size() == 0);
}

BOOST_AUTO_TEST_CASE(TransactionBucket)
{
    testTransactionBucket();
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos