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
#include <libblockchain/BlockChainImp.h>
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
using namespace dev::executive;

namespace dev
{
namespace test
{
class FakeBlockchain
{
public:
    FakeBlockchain() {}
    dev::h256 getHash(int64_t) { return h256(0); }
};

struct BlockVerifierFixture
{
    BlockVerifierFixture()
    {
        m_blockVerifier = std::make_shared<BlockVerifier>();

        auto storagePath = std::string("test_storage/");
        boost::filesystem::create_directories(storagePath);
        leveldb::Options option;
        option.create_if_missing = true;
        option.max_open_files = 100;

        dev::db::BasicLevelDB* dbPtr = NULL;
        leveldb::Status s = dev::db::BasicLevelDB::Open(option, storagePath, &dbPtr);
        if (!s.ok())
        {
            LOG(ERROR) << "Open storage leveldb error: " << s.ToString();
        }

        m_db = std::shared_ptr<dev::db::BasicLevelDB>(dbPtr);

        m_levelDBStorage = std::make_shared<LevelDBStorage>();
        m_levelDBStorage->setDB(m_db);

        m_stateFactory =
            std::make_shared<MPTStateFactory>(u256(0), "test_state", h256(0), WithExisting::Trust);

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

        m_blockVerifier->setExecutiveContextFactory(m_executiveContextFactory);

        m_blockChain = std::make_shared<dev::blockchain::BlockChainImp>();
        m_blockChain->setStateStorage(m_levelDBStorage);

        m_blockVerifier->setNumberHash([this](int64_t num) {
            return this->m_blockChain->getBlockByNumber(num)->headerHash();
        });
        m_fakeBlock = std::make_shared<FakeBlock>(5);
    }

    std::shared_ptr<dev::blockchain::BlockChainImp> m_blockChain;
    std::shared_ptr<BlockVerifier> m_blockVerifier;
    std::shared_ptr<ExecutiveContextFactory> m_executiveContextFactory;
    std::shared_ptr<FakeBlock> m_fakeBlock;
    dev::storage::LevelDBStorage::Ptr m_levelDBStorage;
    std::shared_ptr<dev::db::BasicLevelDB> m_db;
    std::shared_ptr<dev::executive::StateFactoryInterface> m_stateFactory;
    std::unordered_map<Address, dev::eth::PrecompiledContract> m_precompiledContract;
};

BOOST_FIXTURE_TEST_SUITE(BlockVerifierTest, BlockVerifierFixture)


BOOST_AUTO_TEST_CASE(executeBlock)
{
    /* for (int i = 0; i < 100; ++i)
     {
         auto max = m_blockChain->number();

         auto parentBlock = m_blockChain->getBlockByNumber(max);

         dev::eth::BlockHeader header;
         header.setNumber(i);
         header.setParentHash(parentBlock->headerHash());
         header.setGasLimit(dev::u256(1024 * 1024 * 1024));
         header.setRoots(parentBlock->header().transactionsRoot(),
             parentBlock->header().receiptsRoot(), parentBlock->header().stateRoot());

         dev::eth::Block block;
         block.setBlockHeader(header);

         dev::bytes rlpBytes = dev::fromHex(
             "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a10000000"
             "0000"
             "000000000000003ca576d469d7aa0244071d27eb33c5629753593e00000000000000000000000000000000"
             "0000"
             "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62"
             "657c"
             "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
         dev::eth::Transaction tx(ref(rlpBytes), dev::eth::CheckTransaction::Everything);
         block.appendTransaction(tx);

         auto context = m_blockVerifier->executeBlock(block);
         m_blockChain->commitBlock(block, context);
     }*/
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
