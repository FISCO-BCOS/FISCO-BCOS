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
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/TransactionReceipt.h>
#include <libmptstate/MPTStateFactory.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>

using namespace dev;
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
                "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
                "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
                "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
                "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
                "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
                "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
            dev::eth::Transaction tx(ref(rlpBytes), dev::eth::CheckTransaction::Everything);
            dev::KeyPair key_pair(dev::Secret::random());
            dev::Secret sec = key_pair.secret();
            u256 maxBlockLimit = u256(1000);
            tx.setNonce(tx.nonce() + u256(1));
            tx.setBlockLimit(u256(blockChain->number()) + maxBlockLimit);
            dev::Signature sig = sign(sec, tx.sha3(dev::eth::WithoutSignature));
            tx.updateSignature(SignatureStruct(sig));
            LOG(INFO) << "Tx" << tx.sha3();
            block.appendTransaction(tx);
            auto context = blockVerifier->executeBlock(block);
            blockChain->commitBlock(block, context);
            dev::eth::TransactionReceipt receipt =
                blockChain->getTransactionReceiptByHash(tx.sha3());
            LOG(INFO) << "receipt" << receipt;
        }
    }
    else if (argc > 1 && std::string("verify") == argv[1])
    {
    }
}