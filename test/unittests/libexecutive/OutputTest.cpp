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
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcore/CommonJS.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <libexecutive/Executive.h>
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
        g_BCOSConfig.setSupportedVersion("2.3.0", V2_3_0);
        contractAddress = Address("0xa4adafef4c989e17675479565b9abb5821d81f2c");

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

        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        clcPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);

        // createTable
        std::string tableName("c_" + contractAddress.hex());
        table = memoryTableFactory->createTable(tableName, STORAGE_KEY, STORAGE_VALUE, false);
        if (!table)
        {
            LOG(ERROR) << LOG_BADGE("OutputTest") << LOG_DESC("createAccount failed")
                       << LOG_KV("Account", tableName);
        }
        auto entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_BALANCE);
        entry->setField(STORAGE_VALUE, "0");
        table->insert(ACCOUNT_BALANCE, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
        entry->setField(STORAGE_VALUE, toHex(h256("123456")));
        table->insert(ACCOUNT_CODE_HASH, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_KEY, ACCOUNT_CODE);
        entry->setField(STORAGE_VALUE, "");
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
    dev::precompiled::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
    Table::Ptr table;
    Address contractAddress;
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
        fakeHeader.setParentHash(sha3("parent"));
        fakeHeader.setRoots(sha3("transactionRoot"), sha3("receiptRoot"), sha3("stateRoot"));
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
    entry->setField(STORAGE_VALUE, toHex(EmptySHA3));
    table->update(ACCOUNT_CODE_HASH, entry, table->newCondition());
    executeTransaction(*executive, tx);
    output = abi.abiIn("Error(string)", string("Error address:" + contractAddress.hex()));
    o = owning_bytes_ref{std::move(output), 0, output.size()};
    BOOST_TEST(toHex(executive->takeOutput()) == toHex(o));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace OutputTest
