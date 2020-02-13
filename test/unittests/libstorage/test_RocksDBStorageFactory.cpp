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

#include "libstorage/BasicRocksDB.h"
#include "libstorage/RocksDBStorageFactory.h"
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;

namespace test_RocksDBStorageFactory
{
struct StorageFixture
{
    StorageFixture() { options = getRocksDBOptions(); }
    rocksdb::Options options;
    std::string dbPath = "testdata";
};

BOOST_FIXTURE_TEST_SUITE(TestRocksDBStorageFactory, StorageFixture)

BOOST_AUTO_TEST_CASE(Constructor)
{
    RocksDBStorageFactory rf(dbPath, false, true);
    rf.setDBOpitons(options);
    auto rocksdbStorage1 = rf.getStorage("1");
    auto rocksdbStorage2 = rf.getStorage("1");
    BOOST_CHECK(rocksdbStorage1 == rocksdbStorage2);
    auto rocksdbStorage3 = rf.getStorage("2");
    BOOST_CHECK(rocksdbStorage1 != rocksdbStorage3);
    BOOST_CHECK(rocksdbStorage2 != rocksdbStorage3);
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_RocksDBStorageFactory
