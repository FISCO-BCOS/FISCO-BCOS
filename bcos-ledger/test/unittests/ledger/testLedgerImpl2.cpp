#include <bcos-tars-protocol/impl/TarsHashable.h>

#include "bcos-concepts/ledger/Ledger.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-ledger/src/libledger/LedgerImpl2.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Ranges.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::ledger;

struct LedgerImpl2Fixture
{
    LedgerImpl2Fixture() {}
};

BOOST_FIXTURE_TEST_SUITE(Ledger2ImplTest, LedgerImpl2Fixture)

BOOST_AUTO_TEST_CASE(commitBlock)
{
    task::syncWait([]() -> task::Task<void> {
        auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(
            std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
        bcostars::protocol::BlockFactoryImpl blockFactory(cryptoSuite, nullptr, nullptr, nullptr);
        transaction_executor::TableNamePool tableNamePool;
        storage2::memory_storage::MemoryStorage<transaction_executor::StateKey, storage::Entry,
            bcos::storage2::memory_storage::ORDERED>
            storage;

        LedgerImpl2<crypto::hasher::openssl::OpenSSL_Keccak256_Hasher, decltype(storage),
            decltype(blockFactory)>
            ledger(storage, blockFactory, tableNamePool);

        bcostars::protocol::BlockImpl block;
        auto blockHeader = block.blockHeader();
        blockHeader->setNumber(100786);
        blockHeader->setTimestamp(100865);
        blockHeader->calculateHash(*cryptoSuite->hashImpl());

        co_await ledger.setBlock<bcos::concepts::ledger::HEADER, bcos::concepts::ledger::RECEIPTS>(
            block);
    }());
}

BOOST_AUTO_TEST_SUITE_END()