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
 * @file storage_main.cpp
 * @author: xingqiangbai
 * @date 2018-11-14
 */

#include "libinitializer/Initializer.h"
#include "libstorage/MemoryTableFactory.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include <leveldb/db.h>
#include <libstorage/BasicRocksDB.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/MemoryTable2.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstorage/RocksDBStorage.h>
#include <libutilities/BasicLevelDB.h>
#include <libutilities/Common.h>
#include <tbb/parallel_for.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/from_stream.hpp>


using namespace std;
using namespace bcos;
using namespace boost;
using namespace bcos::storage;
using namespace bcos::initializer;

void testMemoryTable2(size_t round, size_t count, bool verify)
{
    boost::filesystem::create_directories("./RocksDB");
    rocksdb::Options options;
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    options.create_if_missing = true;
    options.max_open_files = 1000;
    options.compression = rocksdb::kSnappyCompression;
    rocksdb::Status status;
    auto rocksDB = std::make_shared<BasicRocksDB>();
    rocksDB->Open(options, "./RocksDB");

    std::shared_ptr<RocksDBStorage> rocksdbStorage = std::make_shared<RocksDBStorage>();

    rocksdbStorage->setDB(rocksDB);


    CachedStorage::Ptr cachedStorage = std::make_shared<CachedStorage>();
    cachedStorage->setBackend(rocksdbStorage);
    // cachedStorage->startClearThread();
    cachedStorage->init();
    cachedStorage->setMaxCapacity(32 * 1024 * 1024);
    cachedStorage->setMaxForwardBlock(5);

    auto factoryFactory = std::make_shared<MemoryTableFactoryFactory2>();
    factoryFactory->setStorage(cachedStorage);

    auto createFactory = factoryFactory->newTableFactory(bcos::h256(0), 0);
    createFactory->createTable("test_data", "key", "value", 1, Address(0x0));
    createFactory->createTable("tx_hash_2_block", "tx_hash", "block_hash", 1, Address(0x0));

    createFactory->commitDB(bcos::h256(0), 1);

    std::set<std::string> accounts;
    for (size_t i = 0; i < count; ++i)
    {
        auto dataTable = createFactory->openTable("test_data");
        auto entry = dataTable->newEntry();
        auto key = (boost::format("[%08d]") % i).str();
        entry->setField("key", key);
        entry->setField("value", "0");
        entry->setForce(true);

        dataTable->insert(key, entry);
    }

    createFactory->commitDB(bcos::h256(0), 1);

    auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < round; ++i)
    {
        auto roundStart = std::chrono::steady_clock::now();
        auto factory = factoryFactory->newTableFactory(bcos::h256(0), i + 2);

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, count), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t j = range.begin(); j < range.end(); ++j)
                {
                    for (int k = 0; k < 50; ++k)
                    {
                        auto dataTable = factory->openTable("test_data");
                        auto txTable = factory->openTable("tx_hash_2_block");

                        auto key = (boost::format("[%08d]") % j).str();
                        auto condition = dataTable->newCondition();
                        auto dataEntries = dataTable->select(key, condition);

                        auto dataEntry = dataEntries->get(0);

                        auto newDataEntry = dataTable->newEntry();
                        newDataEntry->setField("value",
                            boost::lexical_cast<std::string>(
                                boost::lexical_cast<size_t>(dataEntry->getField("value")) + 1));

                        dataTable->update(key, newDataEntry, condition);

                        std::string txKey = (boost::format("[%32d]-[%32d]") % j % k).str();
                        auto txEntry = txTable->newEntry();
                        txEntry->setField("tx_hash", txKey);
                        txEntry->setField("block_hash", txKey);
                        txEntry->setForce(true);

                        txTable->insert(txKey, txEntry);
                    }
                }
            });

        factory->commitDB(bcos::h256(0), i + 2);

        auto roundEnd = std::chrono::steady_clock::now();
        std::chrono::duration<double> roundElapsed = roundEnd - roundStart;
        std::cout << "Round " << i << " elapsed: " << roundElapsed.count() << std::endl;

        if (verify)
        {
            std::cout << "Checking round " << i << " ...";

            auto factory = factoryFactory->newTableFactory(bcos::h256(0), round + 2);
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, count), [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t j = range.begin(); j < range.end(); ++j)
                    {
                        auto dataTable = factory->openTable("test_data");
                        auto txTable = factory->openTable("tx_hash_2_block");

                        auto key = (boost::format("[%08d]") % j).str();
                        auto condition = dataTable->newCondition();
                        auto dataEntries = dataTable->select(key, condition);

                        auto dataEntry = dataEntries->get(0);

                        size_t value = boost::lexical_cast<size_t>(dataEntry->getField("value"));
                        if (value != 50 * (i + 1))
                        {
                            std::cout << "Verify failed, value: " << value
                                      << " != " << (i + 1) * 50;
                        }
                    }
                });

            std::cout << "Check round " << i << " finshed";
        }
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Execute time elapsed " << std::setiosflags(std::ios::fixed)
              << std::setprecision(4) << elapsed.count() << std::endl;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    boost::multi_index_container<std::pair<std::string, std::string>,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<
                boost::multi_index::identity<std::pair<std::string, std::string> > > > >
        m_mru;
    m_mru.push_back(std::make_pair("a", "b"));
    m_mru.push_back(std::make_pair("b", "c"));
    m_mru.push_back(std::make_pair("a", "b"));

    for (auto it = m_mru.begin(); it != m_mru.end();)
    {
        std::cout << "item: " << it->first << ", " << it->second;

        it = m_mru.erase(it);
    }

#if 0
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " [round] [count] [verify]" << std::endl;
        return 1;
    }

#if 1
    boost::property_tree::ptree pt;

    boost::property_tree::read_ini("config.ini", pt);

    /// init log
    auto logInitializer = std::make_shared<LogInitializer>();
    logInitializer->initLog(pt);
#endif

    size_t round = boost::lexical_cast<size_t>(argv[1]);
    size_t count = boost::lexical_cast<size_t>(argv[2]);
    bool verify = false;
    if (argc > 3)
    {
        verify = boost::lexical_cast<bool>(argv[3]);
    }

    testMemoryTable2(round, count, verify);
#endif

    return 0;
}
