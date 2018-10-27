/**
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
 *
 * @brief
 *
 * @file BlockVerifierTest.cpp
 * @author:
 * @date 2018-09-21
 */
#include <leveldb/db.h>
#include <libblockverifier/BlockVerifier.h>
#include <libethcore/PrecompiledContract.h>
#include <libmptstate/MPTStateFactory.h>
#include <libstorage/LevelDBStorage.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::mptstate;

namespace dev
{
namespace test
{
class FakeBlockchain
{
public:
    FakeBlockchain() {}
    dev::h256 getHash(int64_t x) { return h256(0); }
};

struct BlockVerifierFixture
{
    BlockVerifierFixture()
    {
        m_blockVerifier = std::make_shared<BlockVerifier>();
        leveldb::Options option;
        option.create_if_missing = true;
        option.max_open_files = 100;

        leveldb::DB* dbPtr = NULL;
        leveldb::Status s = leveldb::DB::Open(option, "./block", &dbPtr);
        if (!s.ok())
        {
            LOG(ERROR) << "Open leveldb error: " << s.ToString();
        }

        m_db = std::shared_ptr<leveldb::DB>(dbPtr);
        m_levelDBStorage = std::make_shared<LevelDBStorage>();
        m_levelDBStorage->setDB(m_db);

        m_stateFactory =
            std::make_shared<MPTStateFactory>(u256(0), "./overlayDB", h256(0), WithExisting::Trust);

        m_precompiledContract.insert(std::make_pair(
            Address(1), PrecompiledContract(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
        m_precompiledContract.insert(std::make_pair(
            Address(2), PrecompiledContract(60, 12, PrecompiledRegistrar::executor("sha256"))));
        m_precompiledContract.insert(std::make_pair(Address(3),
            PrecompiledContract(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
        m_precompiledContract.insert(std::make_pair(
            Address(4), PrecompiledContract(15, 3, PrecompiledRegistrar::executor("identity"))));

        m_executiveContextFactory = std::make_shared<ExecutiveContextFactory>();
        m_executiveContextFactory->setStateStorage(m_levelDBStorage);
        m_executiveContextFactory->setStateFactory(m_stateFactory);
        m_executiveContextFactory->setPrecompiledContract(m_precompiledContract);

        m_blockVerifier->setExecutiveContextFactory(m_executiveContextFactory);

        m_blockVerifier->setNumberHash(
            boost::bind(&FakeBlockchain::getHash, &m_fakeBlockchain, _1));
        m_fakeBlock = std::make_shared<FakeBlock>(5);
    }

    FakeBlockchain m_fakeBlockchain;
    std::shared_ptr<BlockVerifier> m_blockVerifier;
    std::shared_ptr<ExecutiveContextFactory> m_executiveContextFactory;
    std::shared_ptr<FakeBlock> m_fakeBlock;
    dev::storage::LevelDBStorage::Ptr m_levelDBStorage;
    std::shared_ptr<leveldb::DB> m_db;
    std::shared_ptr<dev::eth::StateFactoryInterface> m_stateFactory;
    std::unordered_map<Address, dev::eth::PrecompiledContract> m_precompiledContract;
};

BOOST_FIXTURE_TEST_SUITE(BlockVerifierTest, BlockVerifierFixture);


BOOST_AUTO_TEST_CASE(executeBlock)
{
    // m_blockVerifier->executeBlock(m_fakeBlock->getBlock());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
