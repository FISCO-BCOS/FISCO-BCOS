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

/**
 * @brief : verifierMain
 * @author: mingzhenliu
 * @date: 2018-09-21
 */
#include <leveldb/db.h>
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcore/easylog.h>
#include <libethcore/Block.h>
#include <libmptstate/MPTStateFactory.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
INITIALIZE_EASYLOGGINGPP
int main(int argc, char* argv[])
{
    auto storagePath = std::string("test_storage/");
    boost::filesystem::create_directories(storagePath);
    leveldb::Options option;
    option.create_if_missing = true;
    option.max_open_files = 100;
    leveldb::DB* dbPtr = NULL;
    leveldb::Status s = leveldb::DB::Open(option, storagePath, &dbPtr);
    if (!s.ok())
    {
        LOG(ERROR) << "Open storage leveldb error: " << s.ToString();
    }

    auto storageDB = std::shared_ptr<leveldb::DB>(dbPtr);
    auto storage = std::make_shared<dev::storage::LevelDBStorage>();
    storage->setDB(storageDB);

    auto blockChain = std::make_shared<dev::blockchain::BlockChainImp>();
    blockChain->setStateStorage(storage);


    auto stateFactory = std::make_shared<dev::mptstate::MPTStateFactory>(
        dev::u256(0), "test_state", dev::h256(0), dev::WithExisting::Trust);

    auto executiveContextFactory = std::make_shared<dev::blockverifier::ExecutiveContextFactory>();
    executiveContextFactory->setStateFactory(stateFactory);
    executiveContextFactory->setStateStorage(storage);


    auto blockVerifier = std::make_shared<dev::blockverifier::BlockVerifier>();
    blockVerifier->setExecutiveContextFactory(executiveContextFactory);
    blockVerifier->setNumberHash(
        [blockChain](int64_t num) { return blockChain->getBlockByNumber(num)->headerHash(); });

    if (argc > 1 && std::string("insert") == argv[1])
    {
        for (int i = 0; i < 100; ++i)
        {
            auto max = blockChain->number();
            auto parentBlock = blockChain->getBlockByNumber(max);
            dev::eth::BlockHeader header;
            header.setNumber(i);
            header.setParentHash(parentBlock->headerHash());
            header.setGasLimit(dev::u256(1024 * 1024 * 1024));
            header.setRoots(parentBlock->header().transactionsRoot(),
                parentBlock->header().receiptsRoot(), parentBlock->header().stateRoot());
            dev::eth::Block block;
            block.setBlockHeader(header);
            dev::bytes rlpBytes = dev::fromHex(
                "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a1000"
                "00000000"
                "000000000000003ca576d469d7aa0244071d27eb33c5629753593e0000000000000000000000000000"
                "00000000"
                "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff"
                "6d62657c"
                "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
            dev::eth::Transaction tx(ref(rlpBytes), dev::eth::CheckTransaction::Everything);
            block.appendTransaction(tx);
            auto context = blockVerifier->executeBlock(block);
            blockChain->commitBlock(block, context);
        }
    }
    else if (argc > 1 && std::string("verify") == argv[1])
    {
    }
}