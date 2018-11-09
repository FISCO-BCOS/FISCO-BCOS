// "Copyright [2018] <fisco-dev>"
#include "../libstorage/MemoryStorage.h"
#include "Common.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/CRUDPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;

namespace test_CRUDPrecompiled
{
struct CRUDPrecompiledFixture
{
    CRUDPrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory;
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        crudPrecompiled = std::make_shared<CRUDPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();
    }

    ~CRUDPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    MemoryTableFactory::Ptr memoryTableFactory;
    CRUDPrecompiled::Ptr crudPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(CRUDPrecompiled, CRUDPrecompiledFixture)

BOOST_AUTO_TEST_CASE(call_add)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("select(string,string)", "_sys_miners_", "miner");
    bytes out = crudPrecompiled->call(context, bytesConstRef(&in));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_CRUDPrecompiled
