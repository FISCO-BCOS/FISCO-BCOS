/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file LedgerTest.cpp
 * @author: kyonRay
 * @date 2021-05-07
 * @file LedgerTest.cpp
 * @author: ancelmo
 * @date 2021-09-07
 */

#include "bcos-ledger/src/libledger/Ledger.h"
#include "../../mock/MockKeyFactor.h"
#include "bcos-framework/interfaces/crypto/CommonType.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-ledger/src/libledger/utilities/Common.h"
#include "common/FakeBlock.h"
#include "libtool/ConsensusNode.h"
#include <bcos-codec/scale/Scale.h>
#include <bcos-framework/interfaces/consensus/ConsensusNode.h>
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-framework/interfaces/storage/StorageInterface.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-framework/libstorage/StateStorage.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::crypto;

namespace std
{
inline ostream& operator<<(ostream& os, const std::optional<Entry>& entry)
{
    os << entry.has_value();
    return os;
}

inline ostream& operator<<(ostream& os, const std::optional<Table>& table)
{
    os << table.has_value();
    return os;
}

inline ostream& operator<<(ostream& os, const std::unique_ptr<Error>& error)
{
    os << error->what();
    return os;
}
}  // namespace std

namespace bcos::test
{
class LedgerFixture : public TestPromptFixture
{
public:
    LedgerFixture() : TestPromptFixture()
    {
        m_blockFactory = createBlockFactory(createCryptoSuite());
        auto keyFactor = std::make_shared<MockKeyFactory>();
        m_blockFactory->cryptoSuite()->setKeyFactory(keyFactor);

        BOOST_CHECK(m_blockFactory != nullptr);
        BOOST_CHECK(m_blockFactory->blockHeaderFactory() != nullptr);
        BOOST_CHECK(m_blockFactory->transactionFactory() != nullptr);
        initStorage();
    }
    ~LedgerFixture() = default;

    inline void initStorage()
    {
        auto hashImpl = std::make_shared<Keccak256Hash>();
        auto memoryStorage = std::make_shared<StateStorage>(nullptr);
        memoryStorage->setEnableTraverse(true);
        auto storage = std::make_shared<StateStorage>(memoryStorage);
        storage->setEnableTraverse(true);
        m_storage = storage;
        BOOST_TEST(m_storage != nullptr);
        m_ledger = std::make_shared<Ledger>(m_blockFactory, m_storage);
        BOOST_CHECK(m_ledger != nullptr);
    }

    inline void initErrorStorage()
    {
        auto memoryStorage = std::make_shared<StateStorage>(nullptr);
        memoryStorage->setEnableTraverse(true);
        auto storage = std::make_shared<StateStorage>(memoryStorage);
        storage->setEnableTraverse(true);
        m_storage = storage;
        BOOST_TEST(m_storage != nullptr);
        m_ledger = std::make_shared<Ledger>(m_blockFactory, m_storage);
        BOOST_CHECK(m_ledger != nullptr);
    }

    inline void initFixture()
    {
        m_param = std::make_shared<LedgerConfig>();
        m_param->setBlockNumber(0);
        m_param->setHash(HashType(""));
        m_param->setBlockTxCountLimit(1000);

        auto signImpl = std::make_shared<Secp256k1SignatureImpl>();
        consensus::ConsensusNodeList consensusNodeList;
        consensus::ConsensusNodeList observerNodeList;
        for (int i = 0; i < 4; ++i)
        {
            auto node = std::make_shared<consensus::ConsensusNode>(
                signImpl->generateKeyPair()->publicKey(), 10 + i);
            consensusNodeList.emplace_back(node);
        }
        auto observer_node = std::make_shared<consensus::ConsensusNode>(
            signImpl->generateKeyPair()->publicKey(), -1);
        observerNodeList.emplace_back(observer_node);

        m_param->setConsensusNodeList(consensusNodeList);
        m_param->setObserverNodeList(observerNodeList);

        LEDGER_LOG(TRACE) << "build genesis for first time";
        auto result = m_ledger->buildGenesisBlock(m_param, 3000000000, "");
        BOOST_CHECK(result);
        LEDGER_LOG(TRACE) << "build genesis for second time";
        auto result2 = m_ledger->buildGenesisBlock(m_param, 3000000000, "");
        BOOST_CHECK(result2);
    }

    inline void initEmptyFixture()
    {
        m_param = std::make_shared<LedgerConfig>();
        m_param->setBlockNumber(0);
        m_param->setHash(HashType(""));
        m_param->setBlockTxCountLimit(0);

        auto result1 = m_ledger->buildGenesisBlock(m_param, 3000000000, "");
        BOOST_CHECK(result1);
        auto result2 = m_ledger->buildGenesisBlock(m_param, 30, "");
        BOOST_CHECK(!result2);
        auto result3 = m_ledger->buildGenesisBlock(m_param, 3000000000, "");
        BOOST_CHECK(result3);
    }

    inline void initBlocks(int _number)
    {
        std::promise<bool> fakeBlockPromise;
        auto future = fakeBlockPromise.get_future();
        m_ledger->asyncGetBlockHashByNumber(0, [=, &fakeBlockPromise](Error::Ptr, HashType _hash) {
            m_fakeBlocks = fakeBlocks(
                m_blockFactory->cryptoSuite(), m_blockFactory, 1, 1, _number, _hash.hex());
            fakeBlockPromise.set_value(true);
        });
        future.get();
    }

    inline void initEmptyBlocks(int _number)
    {
        std::promise<bool> fakeBlockPromise;
        auto future = fakeBlockPromise.get_future();
        m_ledger->asyncGetBlockHashByNumber(0, [=, &fakeBlockPromise](Error::Ptr, HashType _hash) {
            m_fakeBlocks = fakeEmptyBlocks(
                m_blockFactory->cryptoSuite(), m_blockFactory, _number, _hash.hex());
            fakeBlockPromise.set_value(true);
        });
        future.get();
    }

    inline void initChain(int _number)
    {
        initBlocks(_number);
        for (int i = 0; i < _number; ++i)
        {
            auto txDataList = std::make_shared<std::vector<bytesConstPtr>>();
            auto txHashList = std::make_shared<protocol::HashList>();
            for (size_t j = 0; j < m_fakeBlocks->at(i)->transactionsSize(); ++j)
            {
                auto txData = m_fakeBlocks->at(i)->transaction(j)->encode(false);
                auto txPointer = std::make_shared<bytes>(txData.begin(), txData.end());
                txDataList->emplace_back(txPointer);
                txHashList->emplace_back(m_fakeBlocks->at(i)->transaction(j)->hash());
            }

            std::promise<bool> p1;
            auto f1 = p1.get_future();
            m_ledger->asyncStoreTransactions(txDataList, txHashList, [=, &p1](Error::Ptr _error) {
                BOOST_CHECK_EQUAL(_error, nullptr);
                p1.set_value(true);
            });
            BOOST_CHECK_EQUAL(f1.get(), true);

            auto& block = m_fakeBlocks->at(i);

            // write transactions
            std::promise<bool> writeTransactions;
            m_ledger->asyncStoreTransactions(txDataList, txHashList, [&](Error::Ptr error) {
                BOOST_CHECK(!error);
                writeTransactions.set_value(true);
            });
            writeTransactions.get_future().get();

            // write other meta data
            std::promise<bool> prewritePromise;
            m_ledger->asyncPrewriteBlock(
                m_storage, block, [&](Error::Ptr&&) { prewritePromise.set_value(true); });

            prewritePromise.get_future().get();
        }
    }

    inline void initEmptyChain(int _number)
    {
        initEmptyBlocks(_number);
        for (int i = 0; i < _number; ++i)
        {
            // std::promise<bool> p2;
            // auto f2 = p2.get_future();
            // m_ledger->asyncStoreReceipts(table, m_fakeBlocks->at(i), [=, &p2](Error::Ptr _error)
            // {
            //     BOOST_CHECK_EQUAL(_error, nullptr);
            //     p2.set_value(true);
            // });
            // BOOST_CHECK_EQUAL(f2.get(), true);

            // std::promise<bool> p3;
            // auto f3 = p3.get_future();
            // m_ledger->asyncCommitBlock(m_fakeBlocks->at(i)->blockHeader(),
            //     [=, &p3](Error::Ptr _error, LedgerConfig::Ptr _config) {
            //         BOOST_CHECK_EQUAL(_error, nullptr);
            //         BOOST_CHECK_EQUAL(_config->blockNumber(), i + 1);
            //         BOOST_CHECK(!_config->consensusNodeList().empty());
            //         BOOST_CHECK(!_config->observerNodeList().empty());
            //         p3.set_value(true);
            //     });

            // BOOST_CHECK_EQUAL(f3.get(), true);

            std::promise<bool> p3;
            m_ledger->asyncPrewriteBlock(m_storage, m_fakeBlocks->at(i), [&](Error::Ptr&& error) {
                BOOST_CHECK(!error);
                p3.set_value(true);
            });
            BOOST_CHECK_EQUAL(p3.get_future().get(), true);
        }
    }

    storage::StorageInterface::Ptr m_storage = nullptr;
    BlockFactory::Ptr m_blockFactory = nullptr;
    std::shared_ptr<Ledger> m_ledger = nullptr;
    LedgerConfig::Ptr m_param;
    BlocksPtr m_fakeBlocks;
};

BOOST_FIXTURE_TEST_SUITE(LedgerTest, LedgerFixture)

BOOST_AUTO_TEST_CASE(testFixtureLedger)
{
    initFixture();
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, BlockNumber _number) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK_EQUAL(_number, 0);
        p1.set_value(true);
    });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBlockHashByNumber(0, [&](Error::Ptr _error, crypto::HashType _hash) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK(_hash != HashType(""));
        m_ledger->asyncGetBlockNumberByHash(_hash, [&](Error::Ptr _error, BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_number, 0);
            p2.set_value(true);
        });
    });

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetBlockDataByNumber(0, HEADER, [&](Error::Ptr _error, Block::Ptr _block) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK(_block != nullptr);
        BOOST_CHECK_EQUAL(_block->blockHeader()->number(), 0);
        p3.set_value(true);
    });

    std::promise<bool> p4;
    auto f4 = p4.get_future();
    m_ledger->asyncGetTotalTransactionCount(
        [&](Error::Ptr _error, int64_t _totalTxCount, int64_t _failedTxCount,
            protocol::BlockNumber _latestBlockNumber) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_totalTxCount, 0);
            BOOST_CHECK_EQUAL(_failedTxCount, 0);
            BOOST_CHECK_EQUAL(_latestBlockNumber, 0);
            p4.set_value(true);
        });

    std::promise<bool> p5;
    auto f5 = p5.get_future();
    m_ledger->asyncGetSystemConfigByKey(
        SYSTEM_KEY_TX_COUNT_LIMIT, [&](Error::Ptr _error, std::string _value, BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_value, "1000");
            BOOST_CHECK_EQUAL(_number, 0);
            p5.set_value(true);
        });

    std::promise<bool> p6;
    auto f6 = p6.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_OBSERVER, [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList->at(0)->nodeID()->hex(),
                m_param->observerNodeList().at(0)->nodeID()->hex());
            p6.set_value(true);
        });

    std::promise<bool> p7;
    std::vector<std::string> v = {"apps", "usr", "sys", "tables"};
    m_storage->asyncGetRow(FS_ROOT, FS_KEY_SUB, [&](Error::UniquePtr, std::optional<Entry> _entry) {
        std::map<std::string, std::string> bfsInfos;
        auto&& out = asBytes(std::string(_entry->getField(0)));
        codec::scale::decode(bfsInfos, gsl::make_span(out));
        for (const auto& item : v)
        {
            BOOST_CHECK(bfsInfos.find(item) != bfsInfos.end());
            std::promise<bool> p;
            m_storage->asyncOpenTable(
                "/" + item, [&](Error::UniquePtr _error, std::optional<Table> _table) {
                    BOOST_CHECK(!_error);
                    BOOST_CHECK(_table.has_value());
                    p.set_value(true);
                });
            p.get_future().get();
        }
        p7.set_value(true);
    });
    p7.get_future().get();
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
    BOOST_CHECK_EQUAL(f4.get(), true);
    BOOST_CHECK_EQUAL(f5.get(), true);
    BOOST_CHECK_EQUAL(f6.get(), true);
}

BOOST_AUTO_TEST_CASE(getBlockNumber)
{
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, BlockNumber _number) {
        BOOST_CHECK(_error != nullptr);
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
        BOOST_CHECK_EQUAL(_number, -1);
        p1.set_value(true);
    });
    BOOST_CHECK_EQUAL(f1.get(), true);
}

BOOST_AUTO_TEST_CASE(getBlockHashByNumber)
{
    initFixture();
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBlockHashByNumber(-1, [=, &p1](Error::Ptr _error, HashType _hash) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
        BOOST_CHECK_EQUAL(_hash, HashType());
        p1.set_value(true);
    });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBlockHashByNumber(1000, [=, &p2](Error::Ptr _error, HashType _hash) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
        BOOST_CHECK_EQUAL(_hash, HashType());
        p2.set_value(true);
    });

    std::promise<Entry> getRowPromise;
    m_storage->asyncGetRow(
        SYS_NUMBER_2_HASH, "0", [&getRowPromise](auto&& error, std::optional<Entry>&& entry) {
            BOOST_CHECK(!error);
            getRowPromise.set_value(std::move(*entry));
        });

    auto oldHashEntry = getRowPromise.get_future().get();

    Entry hashEntry;
    hashEntry.importFields({""});

    // deal with version conflict
    std::promise<Error::UniquePtr> setRowPromise;
    m_storage->asyncSetRow(
        SYS_NUMBER_2_HASH, "0", std::move(std::move(hashEntry)), [&setRowPromise](auto&& error) {
            BOOST_CHECK(!error);

            setRowPromise.set_value(std::move(error));
        });
    setRowPromise.get_future().get();

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetBlockHashByNumber(0, [=, &p3](Error::Ptr _error, HashType _hash) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK_EQUAL(_hash, HashType(""));
        p3.set_value(true);
    });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(getBlockNumberByHash)
{
    initFixture();

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    // error hash
    m_ledger->asyncGetBlockNumberByHash(HashType(), [&](Error::Ptr _error, BlockNumber _number) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
        BOOST_CHECK_EQUAL(_number, -1);
        p1.set_value(true);
    });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBlockNumberByHash(
        HashType("123"), [&](Error::Ptr _error, BlockNumber _number) {
            BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
            BOOST_CHECK_EQUAL(_number, -1);
            p2.set_value(true);
        });

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_storage->asyncGetRow(
        SYS_NUMBER_2_HASH, "0", [&](auto&& error, std::optional<Entry>&& hashEntry) {
            BOOST_CHECK(!error);
            BOOST_CHECK(hashEntry);
            auto hash = bcos::crypto::HashType(
                std::string(hashEntry->getField(0)), bcos::crypto::HashType::FromHex);

            Entry numberEntry;
            m_storage->asyncSetRow(
                SYS_HASH_2_NUMBER, hash.hex(), std::move(numberEntry), [&](auto&& error) {
                    BOOST_CHECK(!error);

                    m_ledger->asyncGetBlockNumberByHash(
                        hash, [&](Error::Ptr error, BlockNumber number) {
                            BOOST_CHECK(!error);
                            BOOST_CHECK_EQUAL(number, -1);

                            p3.set_value(true);
                        });
                });
        });

    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(getTotalTransactionCount)
{
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetTotalTransactionCount(
        [&](Error::Ptr _error, int64_t totalCount, int64_t totalFailed,
            bcos::protocol::BlockNumber _number) {
            BOOST_CHECK(_error != nullptr);
            BOOST_CHECK_EQUAL(totalCount, -1);
            BOOST_CHECK_EQUAL(totalFailed, -1);
            BOOST_CHECK_EQUAL(_number, -1);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    initFixture();
    initChain(5);
    std::promise<bool> p2;
    m_ledger->asyncGetTotalTransactionCount(
        [&](Error::Ptr _error, int64_t totalCount, int64_t totalFailed,
            bcos::protocol::BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(totalCount > 0);
            BOOST_CHECK(totalFailed >= 0);
            BOOST_CHECK_EQUAL(_number, 5);
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(p2.get_future().get(), true);
}

BOOST_AUTO_TEST_CASE(getNodeListByType)
{
    initEmptyFixture();

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    // error type get empty node list
    m_ledger->asyncGetNodeListByType(
        "test", [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList->size(), 0);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_nodeList != nullptr);
            BOOST_CHECK(_nodeList->size() == 0);
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(f2.get(), true);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_OBSERVER, [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_nodeList != nullptr);
            BOOST_CHECK(_nodeList->size() == 0);
            p3.set_value(true);
        });
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(testNodeListByType)
{
    initFixture();
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList->size(), 4);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    std::promise<bool> setSealer1;
    m_storage->asyncGetRow(
        SYS_CONSENSUS, "key", [&](Error::UniquePtr error, std::optional<Entry> entry) {
            BOOST_CHECK(!error);
            BOOST_CHECK(entry);

            auto list = decodeConsensusList(entry->getField(0));
            list.emplace_back(bcos::crypto::HashType("56789").hex(), 100, CONSENSUS_SEALER, "5");

            entry->setField(0, encodeConsensusList(list));
            m_storage->asyncSetRow(
                SYS_CONSENSUS, "key", std::move(*entry), [&](Error::UniquePtr error) {
                    BOOST_CHECK(!error);
                    setSealer1.set_value(true);
                });
        });
    setSealer1.get_future().get();

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList->size(), 4);
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(f2.get(), true);

    // set block number to 5
    initChain(5);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeListPtr _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList->size(), 5);
            p3.set_value(true);
        });
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(getBlockDataByNumber)
{
    initFixture();
    // test cache
    initChain(20);

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    // error number
    m_ledger->asyncGetBlockDataByNumber(
        1000, FULL_BLOCK, [=, &p1](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK(_error != nullptr);
            BOOST_CHECK_EQUAL(_block, nullptr);
            p1.set_value(true);
        });

    std::promise<bool> pp1;
    auto ff1 = pp1.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        -1, FULL_BLOCK, [=, &pp1](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK(_error != nullptr);
            BOOST_CHECK_EQUAL(_block, nullptr);
            pp1.set_value(true);
        });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    // cache hit
    m_ledger->asyncGetBlockDataByNumber(
        15, FULL_BLOCK, [=, &p2](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_block->blockHeader() != nullptr);
            BOOST_CHECK(_block->transactionsSize() != 0);
            BOOST_CHECK(_block->receiptsSize() != 0);
            p2.set_value(true);
        });

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    // cache not hit
    m_ledger->asyncGetBlockDataByNumber(
        3, FULL_BLOCK, [=, &p3](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_block->blockHeader() != nullptr);
            BOOST_CHECK(_block->transactionsSize() != 0);
            BOOST_CHECK(_block->receiptsSize() != 0);
            p3.set_value(true);
        });

    std::promise<bool> p4;
    auto f4 = p4.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        5, TRANSACTIONS, [=, &p4](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_block->transactionsSize() != 0);
            p4.set_value(true);
        });

    std::promise<bool> p5;
    auto f5 = p5.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        5, RECEIPTS, [=, &p5](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_block->receiptsSize() != 0);
            p5.set_value(true);
        });

    std::promise<bool> p6;
    auto f6 = p6.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        0, TRANSACTIONS, [=, &p6](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK_EQUAL(_block->transactionsSize(), 0);
            p6.set_value(true);
        });

    std::promise<bool> p7;
    auto f7 = p7.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        0, RECEIPTS, [=, &p7](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK_EQUAL(_block->receiptsSize(), 0);
            p7.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(ff1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
    BOOST_CHECK_EQUAL(f4.get(), true);
    BOOST_CHECK_EQUAL(f5.get(), true);
    BOOST_CHECK_EQUAL(f6.get(), true);
    BOOST_CHECK_EQUAL(f7.get(), true);
}

BOOST_AUTO_TEST_CASE(getTransactionByHash)
{
    initFixture();
    initChain(5);
    auto hashList = std::make_shared<protocol::HashList>();
    auto errorHashList = std::make_shared<protocol::HashList>();
    auto fullHashList = std::make_shared<protocol::HashList>();
    hashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(0));
    hashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(1));
    hashList->emplace_back(m_fakeBlocks->at(4)->transactionHash(0));
    errorHashList->emplace_back(HashType("123"));
    errorHashList->emplace_back(HashType("456"));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(0));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(1));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(2));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(3));

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBatchTxsByHashList(hashList, true,
        [=, &p1](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_txList != nullptr);

            BOOST_CHECK(_proof->at(m_fakeBlocks->at(3)->transaction(0)->hash().hex()) != nullptr);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBatchTxsByHashList(fullHashList, true,
        [=, &p2](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_txList != nullptr);

            BOOST_CHECK(_proof->at(m_fakeBlocks->at(3)->transaction(0)->hash().hex()) != nullptr);
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(f2.get(), true);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    // error hash list
    m_ledger->asyncGetBatchTxsByHashList(errorHashList, true,
        [=, &p3](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_txList != nullptr);
            BOOST_CHECK(_txList->empty());

            BOOST_CHECK(_proof != nullptr);
            BOOST_CHECK(_proof->empty());
            p3.set_value(true);
        });
    BOOST_CHECK_EQUAL(f3.get(), true);

    std::promise<bool> p4;
    auto f4 = p4.get_future();
    // without proof
    m_ledger->asyncGetBatchTxsByHashList(hashList, false,
        [=, &p4](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_txList != nullptr);

            BOOST_CHECK(_proof == nullptr);
            p4.set_value(true);
        });
    BOOST_CHECK_EQUAL(f4.get(), true);

    std::promise<bool> p5;
    auto f5 = p5.get_future();
    // null hash list
    m_ledger->asyncGetBatchTxsByHashList(nullptr, false,
        [=, &p5](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
            BOOST_CHECK(_txList == nullptr);
            BOOST_CHECK(_proof == nullptr);
            p5.set_value(true);
        });
    BOOST_CHECK_EQUAL(f5.get(), true);
}

BOOST_AUTO_TEST_CASE(getTransactionReceiptByHash)
{
    initFixture();
    initChain(5);

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetTransactionReceiptByHash(m_fakeBlocks->at(3)->transactionHash(0), true,
        [&](Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, MerkleProofPtr _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK_EQUAL(
                _receipt->hash().hex(), m_fakeBlocks->at(3)->receipt(0)->hash().hex());

            BOOST_CHECK(_proof != nullptr);
            p1.set_value(true);
        });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    // without proof
    m_ledger->asyncGetTransactionReceiptByHash(m_fakeBlocks->at(3)->transactionHash(0), false,
        [&](Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, MerkleProofPtr _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK_EQUAL(
                _receipt->hash().hex(), m_fakeBlocks->at(3)->receipt(0)->hash().hex());
            BOOST_CHECK(_proof == nullptr);
            p2.set_value(true);
        });

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    // error hash
    m_ledger->asyncGetTransactionReceiptByHash(HashType(), false,
        [&](Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, MerkleProofPtr _proof) {
            BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
            BOOST_CHECK_EQUAL(_receipt, nullptr);
            BOOST_CHECK(_proof == nullptr);
            p3.set_value(true);
        });

    std::promise<bool> p4;
    auto f4 = p4.get_future();
    m_ledger->asyncGetTransactionReceiptByHash(HashType("123"), true,
        [&](Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, MerkleProofPtr _proof) {
            BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
            BOOST_CHECK_EQUAL(_receipt, nullptr);
            BOOST_CHECK(_proof == nullptr);
            p4.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
    BOOST_CHECK_EQUAL(f4.get(), true);
}

BOOST_AUTO_TEST_CASE(getNonceList)
{
    initFixture();
    initChain(5);

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetNonceList(3, 6,
        [&](Error::Ptr _error,
            std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>> _nonceMap) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_nonceMap != nullptr);
            BOOST_CHECK_EQUAL(_nonceMap->size(), 3);
            p1.set_value(true);
        });

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetNonceList(4, 2,
        [&](Error::Ptr _error,
            std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>> _nonceMap) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_nonceMap != nullptr);
            BOOST_CHECK_EQUAL(_nonceMap->size(), 2);
            p3.set_value(true);
        });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    // error param
    m_ledger->asyncGetNonceList(-1, -5,
        [&](Error::Ptr _error,
            std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>> _nonceMap) {
            BOOST_CHECK(_error != nullptr);
            BOOST_CHECK(_nonceMap == nullptr);
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(preStoreTransaction)
{
    initFixture();
    initBlocks(5);
    auto txBytesList = std::make_shared<std::vector<bytesConstPtr>>();
    auto hashList = std::make_shared<crypto::HashList>();
    for (size_t i = 0; i < m_fakeBlocks->at(3)->transactionsSize(); ++i)
    {
        auto txData = m_fakeBlocks->at(3)->transaction(i)->encode();
        auto txPointer = std::make_shared<bytes>(txData.toBytes());
        auto hash = m_fakeBlocks->at(3)->transaction(i)->hash();
        txBytesList->emplace_back(txPointer);
        hashList->emplace_back(hash);
    }

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncStoreTransactions(txBytesList, hashList, [&](Error::Ptr _error) {
        BOOST_CHECK_EQUAL(_error, nullptr);
        p1.set_value(true);
    });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    // null pointer
    m_ledger->asyncStoreTransactions(txBytesList, nullptr, [&](Error::Ptr _error) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
        p2.set_value(true);
    });

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncStoreTransactions(nullptr, hashList, [&](Error::Ptr _error) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
        p3.set_value(true);
    });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(preStoreReceipt)
{
    // initFixture();
    // initBlocks(5);

    // std::promise<bool> p1;
    // auto f1 = p1.get_future();
    // m_ledger->asyncStoreReceipts(nullptr, m_fakeBlocks->at(1), [&](Error::Ptr _error) {
    //     BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
    //     p1.set_value(true);
    // });
    // BOOST_CHECK_EQUAL(f1.get(), true);
}

BOOST_AUTO_TEST_CASE(getSystemConfig)
{
    initFixture();

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetSystemConfigByKey(
        SYSTEM_KEY_TX_COUNT_LIMIT, [&](Error::Ptr _error, std::string _value, BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_value, "1000");
            BOOST_CHECK_EQUAL(_number, 0);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    initChain(5);

    std::promise<Table> tablePromise;
    m_storage->asyncOpenTable(SYS_CONFIG, [&](auto&& error, std::optional<Table>&& table) {
        BOOST_CHECK(!error);

        tablePromise.set_value(std::move(*table));
    });

    auto table = tablePromise.get_future().get();

    auto oldEntry = table.getRow(SYSTEM_KEY_TX_COUNT_LIMIT);
    auto [txCountLimit, enableNum] = oldEntry->getObject<SystemConfigEntry>();
    BOOST_CHECK_EQUAL(txCountLimit, "1000");
    BOOST_CHECK_EQUAL(enableNum, 0);

    Entry newEntry = table.newEntry();
    newEntry.setObject(SystemConfigEntry{"2000", 5});

    table.setRow(SYSTEM_KEY_TX_COUNT_LIMIT, newEntry);

    std::promise<bool> pp2;
    m_ledger->asyncGetSystemConfigByKey(
        SYSTEM_KEY_TX_COUNT_LIMIT, [&](Error::Ptr _error, std::string _value, BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_value, "2000");
            BOOST_CHECK_EQUAL(_number, 5);
            pp2.set_value(true);
        });
    BOOST_CHECK_EQUAL(pp2.get_future().get(), true);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    // get error key
    m_ledger->asyncGetSystemConfigByKey(
        "test", [&](Error::Ptr _error, std::string _value, BlockNumber _number) {
            BOOST_CHECK(_error->errorCode() == LedgerError::GetStorageError);
            BOOST_CHECK_EQUAL(_value, "");
            BOOST_CHECK_EQUAL(_number, -1);
            p3.set_value(true);
        });
    BOOST_CHECK_EQUAL(f3.get(), true);
}

BOOST_AUTO_TEST_CASE(testEmptyBlock)
{
    initFixture();
    initEmptyChain(20);

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        4, FULL_BLOCK, [&](Error::Ptr _error, bcos::protocol::Block::Ptr _block) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_block != nullptr);
            p1.set_value(true);
        });
    BOOST_CHECK(f1.get());
}

BOOST_AUTO_TEST_CASE(testSyncBlock)
{
    auto block = m_blockFactory->createBlock();
    auto blockHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setNumber(100);

    block->setBlockHeader(blockHeader);

    std::string inputStr = "hello world!";
    bytes input(inputStr.begin(), inputStr.end());

    auto keyPair = m_blockFactory->cryptoSuite()->signatureImpl()->generateKeyPair();
    auto tx = m_blockFactory->transactionFactory()->createTransaction(
        0, "to", input, 200, 300, "chainid", "groupid", 800, keyPair);

    block->appendTransaction(tx);

    auto txs = std::make_shared<std::vector<bytesConstPtr>>();
    auto hashList = std::make_shared<crypto::HashList>();
    auto encoded = tx->encode();
    txs->emplace_back(std::make_shared<bytes>(encoded.begin(), encoded.end()));
    hashList->emplace_back(tx->hash());

    initFixture();

    m_ledger->asyncStoreTransactions(txs, hashList, [](Error::Ptr error) { BOOST_CHECK(!error); });
    m_ledger->asyncPrewriteBlock(m_storage, block, [](Error::Ptr&& error) { BOOST_CHECK(!error); });

    m_ledger->asyncGetBlockDataByNumber(
        100, TRANSACTIONS, [tx](Error::Ptr error, bcos::protocol::Block::Ptr block) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(block->transactionsSize(), 1);
            BOOST_CHECK_EQUAL(block->transaction(0)->hash().hex(), tx->hash().hex());
        });
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
