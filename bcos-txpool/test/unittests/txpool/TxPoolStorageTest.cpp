#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include <boost/test/unit_test.hpp>

struct MockTxValidator : public bcos::txpool::TxValidator
{
    MockTxValidator() : bcos::txpool::TxValidator({{}, {}, {}, {}, {}}) {}

    bcos::protocol::TransactionStatus checkTransaction(
        const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce = false) override
    {
        BOOST_CHECK_EQUAL(onlyCheckLedgerNonce, true);
        return bcos::protocol::TransactionStatus::None;
    }
};

struct TxPoolStorageFixture
{
    TxPoolStorageFixture()
      : txValidator{std::make_shared<MockTxValidator>()},
        txpoolConfig{std::make_shared<bcos::txpool::TxPoolConfig>(
            txValidator, nullptr, nullptr, nullptr, nullptr, 0, 0, false)},
        txpoolStorage(txpoolConfig)
    {}


    std::shared_ptr<MockTxValidator> txValidator;
    std::shared_ptr<bcos::txpool::TxPoolConfig> txpoolConfig;
    bcos::txpool::MemoryStorage txpoolStorage;
    bcos::crypto::Keccak256 hashImpl;
};

BOOST_FIXTURE_TEST_SUITE(TxPoolStorageTest, TxPoolStorageFixture)

BOOST_AUTO_TEST_CASE(dupNonce)
{
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    auto txs = std::make_shared<bcos::protocol::Transactions>();
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->setNonce("12345");
    tx->calculateHash(hashImpl);
    txs->emplace_back(tx);
    BOOST_CHECK_EQUAL(txpoolStorage.batchVerifyAndSubmitTransaction(*blockHeader, txs), true);
}

BOOST_AUTO_TEST_SUITE_END()