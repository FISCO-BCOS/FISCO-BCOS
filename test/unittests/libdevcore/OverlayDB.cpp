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
 * @brief Construct a new boost auto test case object for OverlayDB CRUD
 *
 * @file OverlayDB.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include "libdevcrypto/CryptoInterface.h"
#include <libmptstate/OverlayDB.h>
#include <libutilities/LevelDB.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(OverlayDB, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testOverlayDBKeyExist)
{
    boost::filesystem::path path("./test_overlay_db.db");
    std::unique_ptr<bcos::db::DatabaseFace> dbp(new bcos::db::LevelDB(path));

    bcos::OverlayDB overlayDB(std::move(dbp));
    h256 key("0x2979B90fF15080A5F956bE0dD2dF1A345b120183");
    BOOST_CHECK(overlayDB.exists(key) == false);

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(testOverlayDBKeyValue)
{
    boost::filesystem::path path("./test_overlay_db.db");
    std::unique_ptr<bcos::db::DatabaseFace> dbp(new bcos::db::LevelDB(path));

    bcos::OverlayDB overlayDB(std::move(dbp));
    bcos::OverlayDB overlayDB1(std::move(dbp));

    bcos::EnforceRefs enforceRefs(overlayDB, true);
    bcos::EnforceRefs enforceRefs1(overlayDB1, true);

    h256 key = crypto::Hash("0x2979B90fF15080A5F956bE0dD2dF1A345b120183");
    bytesConstRef value("helloworld");

    overlayDB.insert(key, value);
    overlayDB1.insert(key, value);

    overlayDB.commit();
    overlayDB1.commit();

    BOOST_CHECK(overlayDB.exists(key) == true);

    overlayDB.kill(key);
    overlayDB1.kill(key);

    BOOST_CHECK(overlayDB.exists(key) == true);    // will find key in leveldb
    BOOST_CHECK(overlayDB1.exists(key) == false);  // will find key in leveldb

    auto keyMap = overlayDB.get();
    BOOST_CHECK(keyMap.size() == 0);

    auto keys = overlayDB.keys();
    BOOST_CHECK(keys.size() == 0);

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(testOverlayDBAux)
{
    boost::filesystem::path path("./test_overlay_db.db");
    std::unique_ptr<bcos::db::DatabaseFace> dbp(new bcos::db::LevelDB(path));

    bcos::OverlayDB overlayDB(std::move(dbp));
    bcos::EnforceRefs enforceRefs(overlayDB, true);

    h256 key = crypto::Hash("aux_0x2979B90fF15080A5F956bE0dD2dF1A345b120183");

    string valueStr = "helloworld";
    bytesConstRef value(valueStr);
    overlayDB.insertAux(key, value);
    overlayDB.commit();

    auto valueBytes = overlayDB.lookupAux(key);
    valueStr = bcos::asString(valueBytes);
    BOOST_CHECK(valueStr == "helloworld");

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
