/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

#include "../libstorage/MemoryStorage.h"
#include "libdevcrypto/CryptoInterface.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcore/CommonJS.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <libexecutive/Executive.h>
#include <libprecompiled/ChainGovernancePrecompiled.h>
#include <libprecompiled/ContractLifeCyclePrecompiled.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstoragestate/StorageState.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::executive;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace OutputTest
{
class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};
};
struct OutputFixture
{
    static dev::h256 fakeCallBack(int64_t) { return h256(); }

    OutputFixture()
    {
        g_BCOSConfig.setSupportedVersion("2.5.0", V2_5_0);
        contractAddress = Address("0xa4adafef4c989e17675479565b9abb5821d81f2c");
        accountAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde1");

        context = std::make_shared<ExecutiveContext>();
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context->setBlockInfo(blockInfo);

        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        ExecutiveContextFactory factory;
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();

        tableFactoryPrecompiled = std::make_shared<dev::precompiled::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        clcPrecompiled = context->getPrecompiled(Address(0x1007));
        cgPrecompiled = context->getPrecompiled(Address(0x1008));

        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        clcPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
        cgPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);

        // createTable of contract
        std::string tableName("c_" + contractAddress.hex());
        table = memoryTableFactory->createTable(tableName, STORAGE_KEY, STORAGE_VALUE, false);
        if (!table)
        {
            LOG(ERROR) << LOG_BADGE("OutputTest") << LOG_DESC("create contract failed")
                       << LOG_KV("contract", tableName);
        }
        auto entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_BALANCE);
        entry->setField(STORAGE_VALUE, "0");
        table->insert(ACCOUNT_BALANCE, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
        auto codeHash = h256("123456");
        entry->setField(STORAGE_VALUE, codeHash.data(), codeHash.size);
        table->insert(ACCOUNT_CODE_HASH, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_CODE);
        entry->setField(STORAGE_VALUE, nullptr, 0);
        table->insert(ACCOUNT_CODE, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_NONCE);
        entry->setField(STORAGE_VALUE, "0");
        table->insert(ACCOUNT_NONCE, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_ALIVE);
        entry->setField(STORAGE_VALUE, "true");
        table->insert(ACCOUNT_ALIVE, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_AUTHORITY);
        entry->setField(STORAGE_VALUE, "0000000000000000000000000000000000000000");
        table->insert(ACCOUNT_AUTHORITY, entry);

        // createTable of account
        std::string accountTableName("c_" + accountAddress.hex());
        accountTable =
            memoryTableFactory->createTable(accountTableName, STORAGE_KEY, STORAGE_VALUE, false);
        if (!accountTable)
        {
            LOG(ERROR) << LOG_BADGE("OutputTest") << LOG_DESC("create account failed")
                       << LOG_KV("account", accountTableName);
        }
        entry = accountTable->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_BALANCE);
        entry->setField(STORAGE_VALUE, "0");
        accountTable->insert(ACCOUNT_BALANCE, entry);
        entry = accountTable->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
        entry->setField(STORAGE_VALUE, EmptyHash.data(), EmptyHash.size);
        accountTable->insert(ACCOUNT_CODE_HASH, entry);
        entry = accountTable->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_CODE);
        entry->setField(STORAGE_VALUE, nullptr, 0);
        accountTable->insert(ACCOUNT_CODE, entry);
        entry = accountTable->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_NONCE);
        entry->setField(STORAGE_VALUE, "0");
        accountTable->insert(ACCOUNT_NONCE, entry);
        entry = accountTable->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_ALIVE);
        entry->setField(STORAGE_VALUE, "true");
        accountTable->insert(ACCOUNT_ALIVE, entry);
        entry = accountTable->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_FROZEN);
        entry->setField(STORAGE_VALUE, "true");
        accountTable->insert(ACCOUNT_FROZEN, entry);

        executive = std::make_shared<Executive>();
        EnvInfo envInfo{fakeBlockHeader(), fakeCallBack, 0};
        envInfo.setPrecompiledEngine(context);
        executive->setEnvInfo(envInfo);
        executive->setState(context->getState());
    }

    ~OutputFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr clcPrecompiled;
    Precompiled::Ptr cgPrecompiled;
    dev::precompiled::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
    Table::Ptr table;
    Table::Ptr accountTable;
    Address contractAddress;
    Address accountAddress;
    Executive::Ptr executive;

    void executeTransaction(Executive& _e, Transaction const& _transaction)
    {
        std::shared_ptr<Transaction> tx = std::make_shared<Transaction>(_transaction);
        LOG(INFO) << "init";
        _e.initialize(tx);
        LOG(INFO) << "execute";
        if (!_e.execute())
        {
            LOG(INFO) << "go";
            _e.go();
        }
        LOG(INFO) << "finalize";
        _e.finalize();
    }

    BlockHeader fakeBlockHeader()
    {
        BlockHeader fakeHeader;
        fakeHeader.setParentHash(crypto::Hash("parent"));
        fakeHeader.setRoots(crypto::Hash("transactionRoot"), crypto::Hash("receiptRoot"),
            crypto::Hash("stateRoot"));
        fakeHeader.setLogBloom(LogBloom(0));
        fakeHeader.setNumber(int64_t(0));
        fakeHeader.setGasLimit(u256(3000000000000000000));
        fakeHeader.setGasUsed(u256(1000000));
        uint64_t current_time = utcTime();
        fakeHeader.setTimestamp(current_time);
        fakeHeader.appendExtraDataArray(jsToBytes("0x1020"));
        fakeHeader.setSealer(u256("0x00"));
        std::vector<h512> sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(toPublic(Secret::random()));
        }
        fakeHeader.setSealerList(sealer_list);
        return fakeHeader;
    }
};

BOOST_FIXTURE_TEST_SUITE(Output, OutputFixture)

BOOST_AUTO_TEST_CASE(accountLiftCycle)
{
    eth::ContractABI abi;
    bytes in;
    bytes out;
    s256 result = 0;
    PrecompiledExecResult::Ptr callResult;

    // test frozen account output
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    bytes callData;
    Transaction tx(value, gasPrice, gas, contractAddress, callData);
    tx.forceSender(accountAddress);
    executeTransaction(*executive, tx);
    auto output = abi.abiIn("Error(string)", string("Frozen account:" + accountAddress.hex()));
    dev::owning_bytes_ref o = owning_bytes_ref{std::move(output), 0, output.size()};
    BOOST_TEST(toHex(executive->takeOutput()) == toHex(o));

    // grant
    in = abi.abiIn(
        "grantCommitteeMember(address)", Address("0x000000000000000000000000000000000000000"));
    callResult = cgPrecompiled->call(context, bytesConstRef(&in));
    abi.abiOut(&callResult->execResult(), result);
    BOOST_TEST(result == 1);

    // unfreeze success
    in = abi.abiIn("unfreezeAccount(address)", accountAddress);
    callResult = cgPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == 1);

    // freeze success
    in = abi.abiIn("freezeAccount(address)", accountAddress);
    callResult = cgPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == 1);
}

BOOST_AUTO_TEST_CASE(call)
{
    LOG(INFO) << LOG_BADGE("OutputTest") << LOG_DESC("freeze");

    eth::ContractABI abi;
    bytes in;
    bytes out;
    s256 result = 0;

    // freeze success
    in = abi.abiIn("freeze(address)", contractAddress);
    auto callResult = clcPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == 1);

    // test frozen contract output
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes callData;
    Transaction tx(value, gasPrice, gas, contractAddress, callData);
    tx.forceSender(caller);
    executeTransaction(*executive, tx);
    auto output = abi.abiIn("Error(string)", string("Frozen contract:" + contractAddress.hex()));
    dev::owning_bytes_ref o = owning_bytes_ref{std::move(output), 0, output.size()};
    BOOST_TEST(toHex(executive->takeOutput()) == toHex(o));

    // unfreeze success
    in = abi.abiIn("unfreeze(address)", contractAddress);
    callResult = clcPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == 1);

    // test frozen contract output
    auto entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
    entry->setField(STORAGE_VALUE, EmptyHash.data(), EmptyHash.size);
    table->update(ACCOUNT_CODE_HASH, entry, table->newCondition());
    executeTransaction(*executive, tx);
    output = abi.abiIn("Error(string)", string("Error address:" + contractAddress.hex()));
    o = owning_bytes_ref{std::move(output), 0, output.size()};
    BOOST_TEST(toHex(executive->takeOutput()) == toHex(o));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace OutputTest
