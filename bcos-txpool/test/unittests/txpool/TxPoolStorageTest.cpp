#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-utilities/IOServicePool.h"
#include <bcos-framework/ledger/LedgerConfigState.h>
#include <bcos-tx-validator/TxValidator.h>
#include <bcos-tx-validator/Web3NonceChecker.h>
#include <boost/test/unit_test.hpp>

struct TxPoolStorageFixture
{
    TxPoolStorageFixture()
      : web3NonceChecker{std::make_shared<bcos::txvalidator::Web3NonceChecker>(nullptr)},
        txValidator{std::make_shared<bcos::txvalidator::TxValidator>(
            std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr),
            nullptr, std::make_shared<bcos::ledger::LedgerConfigState>(), nullptr, web3NonceChecker,
            [](bcos::protocol::Transaction const&) { return false; }, "group0", "chain0")},
        txpoolConfig{std::make_shared<bcos::txpool::TxPoolConfig>(
            txValidator, nullptr, nullptr, nullptr, nullptr, web3NonceChecker, 0, 0, false)},
        txpoolStorage(txpoolConfig, *ioServicePool->getIOService())
    {}


    bcos::txvalidator::Web3NonceChecker::Ptr web3NonceChecker;
    std::shared_ptr<bcos::txvalidator::TxValidator> txValidator;
    std::shared_ptr<bcos::txpool::TxPoolConfig> txpoolConfig;
    bcos::IOServicePool::Ptr ioServicePool = std::make_shared<bcos::IOServicePool>(1, "txStorTest");
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
    // Admission checks the group and chain a BCOS transaction declares against the ones the
    // validator was built for; a transaction with neither is rejected as InvalidGroupId before
    // this case gets to the behaviour it is about.
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    tx->calculateHash(hashImpl);
    txs->emplace_back(tx);
    BOOST_CHECK_EQUAL(txpoolStorage.batchVerifyAndSubmitTransaction(blockHeader, txs), true);
}

BOOST_AUTO_TEST_SUITE_END()