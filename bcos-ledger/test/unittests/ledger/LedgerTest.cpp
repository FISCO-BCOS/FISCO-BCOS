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

#include "bcos-ledger/Ledger.h"
#include "../../mock/MockKeyFactor.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/BfsFileFactory.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-tool/VersionConverter.h"
#include <bcos-codec/scale/Scale.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/consensus/ConsensusNode.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-table/src/StateStorage.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/algorithm/hex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::crypto;
using namespace bcos::tool;
using namespace std::string_literals;

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
class MockStorage : public virtual StateStorage
{
public:
    MockStorage(std::shared_ptr<StorageInterface> prev)
      : storage::StateStorageInterface(prev), StateStorage(prev, false)
    {}
    bcos::Error::Ptr setRows(std::string_view tableName,
        RANGES::any_view<std::string_view,
            RANGES::category::random_access | RANGES::category::sized>
            keys,
        RANGES::any_view<std::string_view,
            RANGES::category::random_access | RANGES::category::sized>
            values) override
    {
        for (size_t i = 0; i < keys.size(); ++i)
        {
            Entry e;
            e.set(std::string(values[i]));
            asyncSetRow(tableName, keys[i], e, [](Error::UniquePtr) {});
        }
        return nullptr;
    }
};

class LedgerFixture : public TestPromptFixture
{
public:
    LedgerFixture()
      : TestPromptFixture(), merkleUtility(crypto::hasher::openssl::OpenSSL_Keccak256_Hasher{})
    {
        m_blockFactory = createBlockFactory(createNormalCryptoSuite());
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
        auto hashImpl = std::make_shared<Keccak256>();
        auto memoryStorage = std::make_shared<StateStorage>(nullptr, false);
        memoryStorage->setEnableTraverse(true);
        auto storage = std::make_shared<MockStorage>(memoryStorage);
        storage->setEnableTraverse(true);
        m_storage = storage;
        BOOST_TEST(m_storage != nullptr);
        // set the cache size to 5 to observe the phenomenon of cache hits
        m_ledger = std::make_shared<Ledger>(m_blockFactory, m_storage, 5);
        BOOST_CHECK(m_ledger != nullptr);
    }

    inline void initErrorStorage()
    {
        auto memoryStorage = std::make_shared<StateStorage>(nullptr, false);
        memoryStorage->setEnableTraverse(true);
        auto storage = std::make_shared<StateStorage>(memoryStorage, false);
        storage->setEnableTraverse(true);
        m_storage = storage;
        BOOST_TEST(m_storage != nullptr);
        m_ledger = std::make_shared<Ledger>(m_blockFactory, m_storage, 1000);
        BOOST_CHECK(m_ledger != nullptr);
    }

    inline void initFixture(
        bcos::protocol::BlockVersion version = bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        m_param = std::make_shared<LedgerConfig>();
        m_param->setBlockNumber(0);
        m_param->setHash(HashType(""));
        m_param->setBlockTxCountLimit(1000);

        auto signImpl = std::make_shared<Secp256k1Crypto>();
        consensus::ConsensusNodeList consensusNodeList;
        consensus::ConsensusNodeList observerNodeList;
        for (size_t i = 0; i < 4; ++i)
        {
            auto node = consensus::ConsensusNode{signImpl->generateKeyPair()->publicKey(),
                consensus::Type::consensus_sealer, 10 + i, 0, 0};
            consensusNodeList.emplace_back(node);
        }
        auto observer_node = consensus::ConsensusNode{signImpl->generateKeyPair()->publicKey(),
            consensus::Type::consensus_observer, uint64_t(-1), 0, 0};
        observerNodeList.emplace_back(observer_node);

        m_param->setConsensusNodeList(consensusNodeList);
        m_param->setObserverNodeList(observerNodeList);

        LEDGER_LOG(TRACE) << "build genesis for first time";
        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion = static_cast<uint32_t>(version);

        auto result = m_ledger->buildGenesisBlock(genesisConfig, *m_param);
        BOOST_CHECK(result);
        LEDGER_LOG(TRACE) << "build genesis for second time";
        auto result2 = m_ledger->buildGenesisBlock(genesisConfig, *m_param);
        BOOST_CHECK(result2);
    }

    inline void initEmptyFixture()
    {
        m_param = std::make_shared<LedgerConfig>();
        m_param->setBlockNumber(0);
        m_param->setHash(HashType(""));
        m_param->setBlockTxCountLimit(0);

        GenesisConfig genesisConfig1;
        genesisConfig1.m_txGasLimit = 3000000000;
        genesisConfig1.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        auto result1 = m_ledger->buildGenesisBlock(genesisConfig1, *m_param);
        BOOST_CHECK(result1);

        GenesisConfig genesisConfig2;
        genesisConfig2.m_txGasLimit = 30;
        genesisConfig2.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        auto result2 = m_ledger->buildGenesisBlock(genesisConfig2, *m_param);
        BOOST_CHECK(!result2);

        GenesisConfig genesisConfig3;
        genesisConfig3.m_txGasLimit = 3000000000;
        genesisConfig3.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        auto result3 = m_ledger->buildGenesisBlock(genesisConfig3, *m_param);
        BOOST_CHECK(result3);
    }

    inline void initBlocks(int _number)
    {
        std::promise<bool> fakeBlockPromise;
        auto future = fakeBlockPromise.get_future();
        m_ledger->asyncGetBlockHashByNumber(
            0, [=, &fakeBlockPromise, this](Error::Ptr, HashType _hash) {
                m_fakeBlocks = fakeBlocks(
                    m_blockFactory->cryptoSuite(), m_blockFactory, 1, 1, _number, _hash.hex());
                fakeBlockPromise.set_value(true);
            });
        future.get();
        for (int i = 0; i < _number; ++i)
        {
            auto block = m_fakeBlocks->at(i);
            auto transactions = std::make_shared<ConstTransactions>();
            for (size_t j = 0; j < block->transactionsSize(); ++j)
            {
                auto tx = block->transaction(j);
                transactions->push_back(std::const_pointer_cast<Transaction>(tx));
            }
            m_fakeTransactions.emplace_back(transactions);
        }
    }

    inline void initEmptyBlocks(int _number)
    {
        std::promise<bool> fakeBlockPromise;
        auto future = fakeBlockPromise.get_future();
        m_ledger->asyncGetBlockHashByNumber(
            0, [=, &fakeBlockPromise, this](Error::Ptr, HashType _hash) {
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
            auto txSize = m_fakeBlocks->at(i)->transactionsSize();
            auto txDataList = std::make_shared<std::vector<bytesConstPtr>>();
            auto txHashList = std::make_shared<protocol::HashList>();
            auto txList = std::make_shared<std::vector<Transaction::ConstPtr>>();
            for (size_t j = 0; j < txSize; ++j)
            {
                bcos::bytes txData;
                m_fakeBlocks->at(i)->transaction(j)->encode(txData);
                auto txPointer = std::make_shared<bytes>(txData.begin(), txData.end());
                txDataList->push_back(txPointer);
                txHashList->push_back(m_fakeBlocks->at(i)->transaction(j)->hash());
                txList->push_back(
                    m_blockFactory->transactionFactory()->createTransaction(bcos::ref(txData)));
            }

            std::promise<bool> p1;
            auto f1 = p1.get_future();
            m_ledger->asyncPreStoreBlockTxs(
                txList, m_fakeBlocks->at(i), [=, &p1](Error::Ptr _error) {
                    BOOST_CHECK_EQUAL(_error, nullptr);
                    p1.set_value(true);
                });
            BOOST_CHECK_EQUAL(f1.get(), true);

            auto& block = m_fakeBlocks->at(i);

            // write other meta data
            Features features;
            features.set(Features::Flag::feature_evm_address);
            std::promise<bool> prewritePromise;
            m_ledger->asyncPrewriteBlock(
                m_storage, nullptr, block,
                [&](std::string, Error::Ptr&&) { prewritePromise.set_value(true); }, true,
                features);
            prewritePromise.get_future().get();
            // update nonce logic move to executor
            //            for (size_t j = 0; j < txSize; ++j)
            //            {
            //                auto sender = block->transaction(0)->sender();
            //                auto eoa = Address(sender, Address::FromBinary).hex();
            //
            //                task::syncWait(
            //                    [](decltype(m_ledger) ledger, decltype(eoa) eoa) ->
            //                    task::Task<void> {
            //                        auto entry = co_await ledger->getStorageAt(
            //                            eoa, ledger::ACCOUNT_TABLE_FIELDS::NONCE, 0);
            //                        BOOST_CHECK(entry);
            //                        auto nonce = std::stoull(std::string(entry->get()));
            //                        BOOST_CHECK(nonce > 0);
            //                    }(m_ledger, eoa));
            //            }
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
            m_ledger->asyncPrewriteBlock(
                m_storage, nullptr, m_fakeBlocks->at(i), [&](std::string, Error::Ptr&& error) {
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
    std::vector<ConstTransactionsPtr> m_fakeTransactions;
    bcos::crypto::merkle::Merkle<crypto::hasher::openssl::OpenSSL_Keccak256_Hasher> merkleUtility;
};

BOOST_FIXTURE_TEST_SUITE(LedgerTest, LedgerFixture)

BOOST_AUTO_TEST_CASE(testFixtureLedger)
{
    initFixture();
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK_EQUAL(_number, 0);
        p1.set_value(true);
    });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBlockHashByNumber(0, [&](Error::Ptr _error, crypto::HashType _hash) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK(_hash != HashType(""));
        m_ledger->asyncGetBlockNumberByHash(
            _hash, [&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
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
    m_ledger->asyncGetSystemConfigByKey(SYSTEM_KEY_TX_COUNT_LIMIT,
        [&](Error::Ptr _error, std::string _value, bcos::protocol::BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_value, "1000");
            BOOST_CHECK_EQUAL(_number, 0);
            p5.set_value(true);
        });

    std::promise<bool> p6;
    auto f6 = p6.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_OBSERVER, [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(
                _nodeList.at(0).nodeID->hex(), m_param->observerNodeList().at(0).nodeID->hex());
            p6.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
    BOOST_CHECK_EQUAL(f4.get(), true);
    BOOST_CHECK_EQUAL(f5.get(), true);
    BOOST_CHECK_EQUAL(f6.get(), true);
}

BOOST_AUTO_TEST_CASE(test_3_0_FixtureLedger)
{
    initFixture(bcos::protocol::BlockVersion::V3_0_VERSION);
    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK_EQUAL(_number, 0);
        p1.set_value(true);
    });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBlockHashByNumber(0, [&](Error::Ptr _error, crypto::HashType _hash) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK(_hash != HashType(""));
        m_ledger->asyncGetBlockNumberByHash(
            _hash, [&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
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
    m_ledger->asyncGetSystemConfigByKey(SYSTEM_KEY_TX_COUNT_LIMIT,
        [&](Error::Ptr _error, std::string _value, bcos::protocol::BlockNumber _number) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_value, "1000");
            BOOST_CHECK_EQUAL(_number, 0);
            p5.set_value(true);
        });

    std::promise<bool> p6;
    auto f6 = p6.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_OBSERVER, [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(
                _nodeList.at(0).nodeID->hex(), m_param->observerNodeList().at(0).nodeID->hex());
            p6.set_value(true);
        });

    std::promise<bool> p7;
    std::vector<std::string> v = {"apps", "usr", "sys", "tables"};
    m_storage->asyncGetRow(
        tool::FS_ROOT, tool::FS_KEY_SUB, [&](Error::UniquePtr, std::optional<Entry> _entry) {
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
    m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
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
    m_ledger->asyncGetBlockNumberByHash(
        HashType(), [&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
            BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::GetStorageError);
            BOOST_CHECK_EQUAL(_number, -1);
            p1.set_value(true);
        });

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBlockNumberByHash(
        HashType("123"), [&](Error::Ptr _error, bcos::protocol::BlockNumber _number) {
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
                std::string(hashEntry->getField(0)), bcos::crypto::HashType::FromBinary);

            Entry numberEntry;
            m_storage->asyncSetRow(SYS_HASH_2_NUMBER,
                std::string_view((const char*)hash.data(), hash.size()), std::move(numberEntry),
                [&](auto&& error) {
                    BOOST_CHECK(!error);

                    m_ledger->asyncGetBlockNumberByHash(
                        hash, [&](Error::Ptr error, bcos::protocol::BlockNumber number) {
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
        "test", [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList.size(), 0);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_nodeList.empty());
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(f2.get(), true);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_OBSERVER, [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_nodeList.empty());
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
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeList const& _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList.size(), 4);
            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    std::promise<bool> setSealer1;
    auto nodeList = task::syncWait(ledger::getNodeList(*m_storage));
    nodeList.emplace_back(consensus::ConsensusNode{
        std::make_shared<KeyImpl>(bcos::crypto::HashType("56789").asBytes()),
        consensus::Type::consensus_sealer, 100, 0, 5});
    task::syncWait(ledger::setNodeList(*m_storage, nodeList));

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList.size(), 4);
            p2.set_value(true);
        });
    BOOST_CHECK_EQUAL(f2.get(), true);

    // set block number to 5
    initChain(5);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncGetNodeListByType(
        CONSENSUS_SEALER, [&](Error::Ptr _error, consensus::ConsensusNodeList _nodeList) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(_nodeList.size(), 5);
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
    std::promise<bool> p8;
    auto f8 = p8.get_future();
    m_ledger->asyncGetBlockDataByNumber(
        15, TRANSACTIONS_HASH, [=, &p8](Error::Ptr _error, Block::Ptr _block) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK_EQUAL(_block->transactionsSize(), 0);
            BOOST_CHECK_EQUAL(_block->receiptsSize(), 0);
            BOOST_TEST(_block->transactionsMetaDataSize() != 0);
            p8.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);
    BOOST_CHECK_EQUAL(ff1.get(), true);
    BOOST_CHECK_EQUAL(f2.get(), true);
    BOOST_CHECK_EQUAL(f3.get(), true);
    BOOST_CHECK_EQUAL(f4.get(), true);
    BOOST_CHECK_EQUAL(f5.get(), true);
    BOOST_CHECK_EQUAL(f6.get(), true);
    BOOST_CHECK_EQUAL(f7.get(), true);
    BOOST_CHECK_EQUAL(f8.get(), true);
}

BOOST_AUTO_TEST_CASE(getTransactionByHash)
{
    initFixture();
    initChain(10);
    auto hashList = std::make_shared<protocol::HashList>();
    auto errorHashList = std::make_shared<protocol::HashList>();
    auto fullHashList = std::make_shared<protocol::HashList>();
    auto fullHashList1 = std::make_shared<protocol::HashList>();
    hashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(0));
    hashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(1));
    hashList->emplace_back(m_fakeBlocks->at(4)->transactionHash(0));
    errorHashList->emplace_back(HashType("123"));
    errorHashList->emplace_back(HashType("456"));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(0));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(1));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(2));
    fullHashList->emplace_back(m_fakeBlocks->at(3)->transactionHash(3));
    fullHashList1->emplace_back(m_fakeBlocks->at(1)->transactionHash(0));
    fullHashList1->emplace_back(m_fakeBlocks->at(2)->transactionHash(0));
    fullHashList1->emplace_back(m_fakeBlocks->at(3)->transactionHash(0));
    fullHashList1->emplace_back(m_fakeBlocks->at(4)->transactionHash(0));
    fullHashList1->emplace_back(m_fakeBlocks->at(5)->transactionHash(0));

    auto fillCache = [this, &fullHashList1]() {
        std::promise<bool> p;
        auto f = p.get_future();
        m_ledger->asyncGetBatchTxsByHashList(fullHashList1, true,
            [=, &p, this](Error::Ptr _error, protocol::TransactionsPtr _txList,
                std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
                BOOST_CHECK_EQUAL(_error, nullptr);
                BOOST_CHECK(_txList != nullptr);

                for (size_t i = 0; i < fullHashList1->size(); ++i)
                {
                    auto& hash = (*fullHashList1)[i];
                    BOOST_CHECK(_proof->at(hash.hex()) != nullptr);
                    auto ret = merkleUtility.verifyMerkleProof(*_proof->at(hash.hex()), hash,
                        m_fakeBlocks->at(i + 1)->blockHeader()->txsRoot());
                    BOOST_CHECK(ret);
                }

                BOOST_CHECK(
                    _proof->at(m_fakeBlocks->at(3)->transaction(0)->hash().hex()) != nullptr);
                p.set_value(true);
            });
        BOOST_CHECK_EQUAL(f.get(), true);
    };

    LEDGER_LOG(INFO) << "------------------------------------------------------------";
    // fill the cache
    fillCache();
    // access the cache
    fillCache();
    LEDGER_LOG(INFO) << "------------------------------------------------------------";

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetBatchTxsByHashList(hashList, true,
        [=, &p1, this](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_txList != nullptr);

            auto hash = m_fakeBlocks->at(3)->transaction(0)->hash();
            BOOST_CHECK(_proof->at(hash.hex()) != nullptr);
            auto ret = merkleUtility.verifyMerkleProof(
                *_proof->at(hash.hex()), hash, m_fakeBlocks->at(3)->blockHeader()->txsRoot());
            BOOST_CHECK(ret);

            hash = m_fakeBlocks->at(3)->transaction(1)->hash();
            BOOST_CHECK(_proof->at(hash.hex()) != nullptr);
            ret = merkleUtility.verifyMerkleProof(
                *_proof->at(hash.hex()), hash, m_fakeBlocks->at(3)->blockHeader()->txsRoot());
            BOOST_CHECK(ret);

            hash = m_fakeBlocks->at(4)->transaction(0)->hash();
            BOOST_CHECK(_proof->at(hash.hex()) != nullptr);
            ret = merkleUtility.verifyMerkleProof(
                *_proof->at(hash.hex()), hash, m_fakeBlocks->at(4)->blockHeader()->txsRoot());
            BOOST_CHECK(ret);

            p1.set_value(true);
        });
    BOOST_CHECK_EQUAL(f1.get(), true);

    std::promise<bool> p2;
    auto f2 = p2.get_future();
    m_ledger->asyncGetBatchTxsByHashList(fullHashList, true,
        [=, &p2, this](Error::Ptr _error, protocol::TransactionsPtr _txList,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>> _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK(_txList != nullptr);

            for (auto& hash : *fullHashList)
            {
                BOOST_CHECK(_proof->at(hash.hex()) != nullptr);
                auto ret = merkleUtility.verifyMerkleProof(
                    *_proof->at(hash.hex()), hash, m_fakeBlocks->at(3)->blockHeader()->txsRoot());
                BOOST_CHECK(ret);
            }

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
            BOOST_CHECK(_error != nullptr);
            BOOST_CHECK(_txList == nullptr);

            BOOST_CHECK(_proof == nullptr);
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
    initChain(10);

    auto checkGetReceipt = [this](int blockNumber, int hashIndex) {
        std::promise<bool> p1;
        auto f1 = p1.get_future();
        m_ledger->asyncGetTransactionReceiptByHash(
            m_fakeBlocks->at(blockNumber)->transactionHash(hashIndex), true,
            [&](Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, MerkleProofPtr _proof) {
                BOOST_CHECK_EQUAL(_error, nullptr);
                BOOST_CHECK_EQUAL(_receipt->hash().hex(),
                    m_fakeBlocks->at(blockNumber)->receipt(hashIndex)->hash().hex());

                auto hash = _receipt->hash();
                BOOST_CHECK(_proof != nullptr);
                auto ret = merkleUtility.verifyMerkleProof(
                    *_proof, hash, m_fakeBlocks->at(blockNumber)->blockHeader()->receiptsRoot());
                BOOST_CHECK(ret);

                BOOST_CHECK(_proof != nullptr);
                p1.set_value(true);
            });
    };

    LEDGER_LOG(INFO) << "------------------------------------------------------------";
    // fill the cache
    checkGetReceipt(1, 0);
    checkGetReceipt(2, 0);
    checkGetReceipt(3, 0);
    checkGetReceipt(4, 0);
    checkGetReceipt(5, 0);
    // access the cache
    checkGetReceipt(1, 0);
    checkGetReceipt(2, 0);
    checkGetReceipt(3, 0);
    checkGetReceipt(4, 0);
    checkGetReceipt(5, 0);
    LEDGER_LOG(INFO) << "------------------------------------------------------------";

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncGetTransactionReceiptByHash(m_fakeBlocks->at(3)->transactionHash(0), true,
        [&](Error::Ptr _error, TransactionReceipt::ConstPtr _receipt, MerkleProofPtr _proof) {
            BOOST_CHECK_EQUAL(_error, nullptr);
            BOOST_CHECK_EQUAL(
                _receipt->hash().hex(), m_fakeBlocks->at(3)->receipt(0)->hash().hex());

            auto hash = _receipt->hash();
            BOOST_CHECK(_proof != nullptr);
            auto ret = merkleUtility.verifyMerkleProof(
                *_proof, hash, m_fakeBlocks->at(3)->blockHeader()->receiptsRoot());
            BOOST_CHECK(ret);

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
    auto block = m_fakeBlocks->at(3);
    auto transactions = m_fakeTransactions[3];

    std::promise<bool> p1;
    auto f1 = p1.get_future();
    m_ledger->asyncPreStoreBlockTxs(transactions, block, [&](Error::Ptr _error) {
        BOOST_CHECK_EQUAL(_error, nullptr);
        p1.set_value(true);
    });
    BOOST_CHECK_EQUAL(f1.get(), true);

#if 0
    std::promise<bool> p2;
    auto f2 = p2.get_future();
    // null pointer
    m_ledger->asyncPreStoreBlockTxs(transactions, nullptr, [&](Error::Ptr _error) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
        p2.set_value(true);
    });
    BOOST_CHECK_EQUAL(f2.get(), true);

    std::promise<bool> p3;
    auto f3 = p3.get_future();
    m_ledger->asyncPreStoreBlockTxs(nullptr, block, [&](Error::Ptr _error) {
        BOOST_CHECK_EQUAL(_error->errorCode(), LedgerError::ErrorArgument);
        p3.set_value(true);
    });
    BOOST_CHECK_EQUAL(f3.get(), true);
#endif
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
    m_ledger->asyncGetSystemConfigByKey(SYSTEM_KEY_TX_COUNT_LIMIT,
        [&](Error::Ptr _error, std::string _value, bcos::protocol::BlockNumber _number) {
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
    m_ledger->asyncGetSystemConfigByKey(SYSTEM_KEY_TX_COUNT_LIMIT,
        [&](Error::Ptr _error, std::string _value, bcos::protocol::BlockNumber _number) {
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
        "test", [&](Error::Ptr _error, std::string _value, bcos::protocol::BlockNumber _number) {
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
    blockHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());

    block->setBlockHeader(blockHeader);

    std::string inputStr = "hello world!";
    bytes input(inputStr.begin(), inputStr.end());

    bcos::crypto::KeyPairInterface::Ptr keyPair =
        m_blockFactory->cryptoSuite()->signatureImpl()->generateKeyPair();
    auto tx = m_blockFactory->transactionFactory()->createTransaction(
        0, "to", input, std::to_string(200), 300, "chainid", "groupid", 800, keyPair);

    block->appendTransaction(tx);
    auto blockTxs = std::make_shared<ConstTransactions>();
    blockTxs->push_back(tx);
    auto txs = std::make_shared<std::vector<bytesConstPtr>>();
    auto hashList = std::make_shared<crypto::HashList>();
    bcos::bytes encoded;
    tx->encode(encoded);
    txs->emplace_back(std::make_shared<bytes>(encoded.begin(), encoded.end()));
    hashList->emplace_back(tx->hash());

    initFixture();
    auto transactions = std::make_shared<Transactions>();
    transactions->push_back(tx);
    m_ledger->asyncPrewriteBlock(
        m_storage, blockTxs, block, [](std::string, Error::Ptr&& error) { BOOST_CHECK(!error); });

    m_ledger->asyncGetBlockDataByNumber(
        100, TRANSACTIONS, [tx](Error::Ptr error, bcos::protocol::Block::Ptr block) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(block->transactionsSize(), 1);
            BOOST_CHECK_EQUAL(block->transaction(0)->hash().hex(), tx->hash().hex());
        });
}

BOOST_AUTO_TEST_CASE(getLedgerConfig)
{
    task::syncWait([this]() -> task::Task<void> {
        initFixture();

        using KeyType = transaction_executor::StateKey;
        Entry value;
        SystemConfigEntry config;

        config = {"12", 0};
        value.setObject(config);
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CONFIG, SYSTEM_KEY_TX_COUNT_LIMIT}, value);

        config = {"100", 0};
        value.setObject(config);
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CONFIG, SYSTEM_KEY_CONSENSUS_LEADER_PERIOD}, value);

        config = {"200", 0};
        value.setObject(config);
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CONFIG, SYSTEM_KEY_TX_GAS_LIMIT}, value);

        config = {"3.8.1", 0};
        value.setObject(config);
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CONFIG, SYSTEM_KEY_COMPATIBILITY_VERSION}, value);

        value.set("10086");
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER}, value);

        auto randomHash = crypto::HashType::generateRandomFixedBytes();
        value.set(randomHash.asBytes());
        co_await storage2::writeOne(*m_storage, KeyType{SYS_NUMBER_2_HASH, "10086"}, value);

        config = {"1", 0};
        value.setObject(config);
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CONFIG, SYSTEM_KEY_RPBFT_SWITCH}, value);

        config = {"12345", 0};
        value.setObject(config);
        co_await storage2::writeOne(
            *m_storage, KeyType{SYS_CONFIG, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM}, value);

        auto ledgerConfig = std::make_shared<LedgerConfig>();
        co_await ledger::getLedgerConfig(*m_ledger, *ledgerConfig);
        BOOST_CHECK_EQUAL(ledgerConfig->blockTxCountLimit(), 12);
        BOOST_CHECK_EQUAL(ledgerConfig->leaderSwitchPeriod(), 100);
        BOOST_CHECK_EQUAL(std::get<0>(ledgerConfig->gasLimit()), 200);
        BOOST_CHECK_EQUAL(ledgerConfig->compatibilityVersion(), tool::toVersionNumber("3.8.1"));
        BOOST_CHECK_EQUAL(ledgerConfig->blockNumber(), 10086);
        BOOST_CHECK_EQUAL(ledgerConfig->hash(), randomHash);
        BOOST_CHECK_EQUAL(ledgerConfig->consensusType(), RPBFT_CONSENSUS_TYPE);

        BOOST_CHECK_EQUAL(std::get<0>(ledgerConfig->epochSealerNum()), 12345);
        BOOST_CHECK_EQUAL(std::get<0>(ledgerConfig->epochBlockNum()), 1000);
        BOOST_CHECK_EQUAL(ledgerConfig->notifyRotateFlagInfo(), 0);
    }());
}

BOOST_AUTO_TEST_CASE(genesisBlockWithAllocs)
{
    task::syncWait([this]() -> task::Task<void> {
        auto hashImpl = std::make_shared<Keccak256>();
        auto memoryStorage = std::make_shared<StateStorage>(nullptr, false);
        auto storage = std::make_shared<MockStorage>(memoryStorage);
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        auto code = "I am a solidity code!"s;
        std::string hexCode;
        boost::algorithm::hex_lower(code, std::back_inserter(hexCode));

        genesisConfig.m_allocs = RANGES::views::iota(0, 10) |
                                 RANGES::views::transform([&](int index) {
                                     Alloc alloc{.address = fmt::format("{:0>40}", index),
                                         .balance = bcos::u256(index * 10),
                                         .nonce = {},
                                         .code = hexCode,
                                         .storage = {}};

                                     if (index % 2 == 0)
                                     {
                                         alloc.storage.emplace_back(fmt::format("{:0>64}", index),
                                             fmt::format("{:0>64}", index * 2));
                                     }
                                     return alloc;
                                 }) |
                                 RANGES::to<std::vector>();

        co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param);

        for (auto i : RANGES::views::iota(0, 10))
        {
            auto tableName = fmt::format("{}{:0>40}", SYS_DIRECTORY::USER_APPS, i);
            auto codeHashEntry = co_await storage2::readOne(*storage,
                transaction_executor::StateKeyView(tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH));

            auto codeEntry = co_await storage2::readOne(*storage,
                transaction_executor::StateKeyView(ledger::SYS_CODE_BINARY, codeHashEntry->get()));
            BOOST_CHECK_EQUAL(codeEntry->get(), code);
            auto codeView = codeEntry->get();
            auto codeHash = hashImpl->hash(
                bcos::bytesConstRef((const uint8_t*)codeView.data(), codeView.size()));
            auto codeHashBytes = codeHash.asBytes();
            BOOST_CHECK_EQUAL(codeHashEntry->get(),
                std::string_view((const char*)codeHashBytes.data(), codeHashBytes.size()));
        }
    }());
}

BOOST_AUTO_TEST_CASE(replaceBinary)
{
    task::syncWait([this]() -> task::Task<void> {
        auto hashImpl = std::make_shared<Keccak256>();
        auto memoryStorage = std::make_shared<StateStorage>(nullptr, false);
        auto storage = std::make_shared<MockStorage>(memoryStorage);
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_5_VERSION);
        auto code = "I am a solidity code!"s;
        std::string hexCode;
        boost::algorithm::hex_lower(code, std::back_inserter(hexCode));

        co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param);

        // Delete the bugfix_statestorage_hash
        BOOST_CHECK(co_await storage2::existsOne(*storage,
            transaction_executor::StateKeyView(ledger::SYS_CONFIG, "bugfix_statestorage_hash")));
        Entry entry;
        entry.setStatus(Entry::DELETED);
        co_await storage2::writeOne(*storage,
            transaction_executor::StateKey(ledger::SYS_CONFIG, "bugfix_statestorage_hash"), entry);

        co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param);
        auto result = co_await storage2::readOne(*storage,
            transaction_executor::StateKeyView(ledger::SYS_CONFIG, "bugfix_statestorage_hash"));
        BOOST_REQUIRE(result);
    }());
}

BOOST_AUTO_TEST_CASE(nonceList)
{
    task::syncWait([this]() -> task::Task<void> {
        auto hashImpl = std::make_shared<Keccak256>();
        auto memoryStorage = std::make_shared<StateStorage>(nullptr, false);
        auto storage = std::make_shared<MockStorage>(memoryStorage);
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        auto nonceList = std::vector<std::string>{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        auto block = m_blockFactory->createBlock();
        block->setNonceList(nonceList);

        co_await storage2::writeOne(*storage,
            transaction_executor::StateKey(SYS_TABLES, SYS_BLOCK_NUMBER_2_NONCES),
            storage::Entry{std::string_view{"value"}});

        bytes buffer;
        block->encode(buffer);
        Entry nonceEntry{buffer};
        co_await storage2::writeOne(
            *storage, transaction_executor::StateKey(SYS_BLOCK_NUMBER_2_NONCES, "1"), nonceEntry);

        auto gotNonceList = co_await ledger::getNonceList(*ledger, 1, 1);
        BOOST_REQUIRE(gotNonceList);
        BOOST_CHECK_EQUAL(gotNonceList->size(), 1);
        auto list = gotNonceList->at(1);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            list->begin(), list->end(), nonceList.begin(), nonceList.end());

        auto gotNonceList2 = co_await ledger::getNonceList(*ledger, 1, 100);
        BOOST_REQUIRE(gotNonceList2);
        BOOST_CHECK_EQUAL(gotNonceList2->size(), 1);
        auto list2 = gotNonceList2->at(1);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            list2->begin(), list2->end(), nonceList.begin(), nonceList.end());

        auto gotNonceList3 = co_await ledger::getNonceList(*ledger, 2, 1);
        BOOST_REQUIRE(gotNonceList3);
        BOOST_CHECK_EQUAL(gotNonceList3->size(), 0);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
