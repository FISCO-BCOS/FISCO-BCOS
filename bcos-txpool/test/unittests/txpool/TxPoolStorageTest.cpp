#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tx-validator/TxValidator.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include "bcos-utilities/IOServicePool.h"
#include <boost/test/unit_test.hpp>

struct MockTxValidator : public bcos::txpool::TxValidator
{
    MockTxValidator() : bcos::txpool::TxValidator({{}, {}, {}, {}, {}}) {}
};

struct TxPoolStorageFixture
{
    TxPoolStorageFixture()
      : txValidator{std::make_shared<MockTxValidator>()},
        txpoolConfig{std::make_shared<bcos::txpool::TxPoolConfig>(
            txValidator, nullptr, nullptr, nullptr, nullptr, 0, 0, false)},
        txpoolStorage(txpoolConfig, *ioServicePool->getIOService())
    {
        // enforceSubmitTransaction must query the BCOS nonce with onlyCheckLedgerNonce=true --
        // a proposal's transactions are not yet in this node's pool, so consulting the pool
        // index would reject every one of them. The assertion used to live in a
        // TxValidatorInterface override; the same contract is now carried by the admission
        // context, which PoolNonceQuery receives as that flag.
        auto admission = std::make_shared<bcos::txvalidator::TxValidator>(
            std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr),
            nullptr,
            []() -> bcos::task::Task<bcos::ledger::LedgerConfig::Ptr> {
                co_return std::make_shared<bcos::ledger::LedgerConfig>();
            },
            [](std::string_view)
                -> bcos::task::Task<std::optional<bcos::txvalidator::AccountState>> {
                co_return std::nullopt;
            },
            [](std::string_view) -> bcos::task::Task<std::optional<bcos::u256>> {
                co_return std::nullopt;
            },
            [](bcos::protocol::Transaction const&) { return false; }, "", "");
        txpoolConfig->setAdmission(
            std::move(admission), bcos::txvalidator::PoolNonceQuery{
                                      .checkBcosNonce = [](bcos::protocol::Transaction const&,
                                                            bool onlyCheckLedgerNonce) {
                                          BOOST_CHECK_EQUAL(onlyCheckLedgerNonce, true);
                                          return bcos::protocol::TransactionStatus::None;
                                      }});
    }


    std::shared_ptr<MockTxValidator> txValidator;
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
    tx->calculateHash(hashImpl);
    txs->emplace_back(tx);
    BOOST_CHECK_EQUAL(txpoolStorage.batchVerifyAndSubmitTransaction(blockHeader, txs), true);
}

BOOST_AUTO_TEST_SUITE_END()