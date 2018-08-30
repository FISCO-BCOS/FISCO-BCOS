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
 * @brief Construct a new boost auto test case object for LevelDB CRUD
 *
 * @file LevelDB.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libdevcore/LevelDB.h>
#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(LevelDB)

BOOST_AUTO_TEST_CASE(testLevelDBKeyExist)
{
    boost::filesystem::path path("./test_level_db.db");
    dev::db::LevelDB ldb(path);
    std::string key = "hellow";
    dev::db::Slice sliceKey(key.data(), key.size());
    BOOST_CHECK(ldb.exists(sliceKey) == false);
    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(testLevelDBKeyValue)
{
    boost::filesystem::path path("./test_level_db.db");
    dev::db::LevelDB ldb(path);
    std::string key = "hello";
    std::string value = "world";
    dev::db::Slice sliceKey(key.data(), key.size());
    dev::db::Slice sliceValue(value.data(), value.size());
    ldb.insert(sliceKey, sliceValue);

    // check key exist
    BOOST_CHECK(ldb.exists(sliceKey) == true);

    // check value eq
    std::string ret = ldb.lookup(sliceKey);
    BOOST_CHECK(ret == value);

    ldb.kill(sliceKey);
    BOOST_CHECK(ldb.exists(sliceKey) == false);
    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(testLevelDBBatch)
{
    boost::filesystem::path path("./test_level_db.db");
    dev::db::LevelDB ldb(path);
    auto wb = ldb.createWriteBatch();

    for (int i = 0; i < 10; i++)
    {
        std::string key = "hello" + std::to_string(i);
        std::string value = "world" + std::to_string(i);
        dev::db::Slice sliceKey(key.data(), key.size());
        dev::db::Slice sliceValue(value.data(), value.size());
        wb->insert(sliceKey, sliceValue);
        BOOST_CHECK(ldb.exists(sliceKey) == false);
    }

    ldb.commit(std::move(wb));

    for (int i = 0; i < 10; i++)
    {
        std::string key = "hello" + std::to_string(i);
        std::string value = "world" + std::to_string(i);
        dev::db::Slice sliceKey(key.data(), key.size());
        std::string ret = ldb.lookup(sliceKey);
        BOOST_CHECK(ret == value);
        ldb.kill(sliceKey);
    }

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(testLevelDBForeach)
{
    boost::filesystem::path path("./test_level_db.db");
    dev::db::LevelDB ldb(path);

    std::string key = "hello";
    std::string value = "world";
    dev::db::Slice sliceKey(key.data(), key.size());
    dev::db::Slice sliceValue(value.data(), value.size());
    ldb.insert(sliceKey, sliceValue);

    int count = 0;
    auto f = [&](dev::db::Slice, dev::db::Slice) {
        count++;
        return true;
    };

    ldb.forEach(f);

    BOOST_CHECK(count == 1);

    ldb.kill(sliceKey);

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_SUITE_END()
