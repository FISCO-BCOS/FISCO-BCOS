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
 * @brief : Encrypted level db unitest
 * @author: jimmyshi
 * @date: 2018-11-28
 */

#include <leveldb/db.h>
#include <leveldb/slice.h>
#include <leveldb/status.h>
#include <libdevcore/BasicLevelDB.h>
#include <libdevcore/LevelDB.h>
#include <libsecurity/EncryptedLevelDB.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::db;

namespace dev
{
namespace test
{
class EncryptedLevelDBFixture : public TestOutputHelperFixture
{
public:
    // static leveldb::ReadOptions defaultReadOptions();
    // static leveldb::WriteOptions defaultWriteOptions();
    // static leveldb::Options defaultDBOptions();
    using Slice = leveldb::Slice;

    shared_ptr<BasicLevelDB> openEncryptedDB(const string& _name)
    {
        if (boost::filesystem::exists(_name))
        {
            boost::filesystem::remove_all(_name);
        }
        dev::db::BasicLevelDB* pleveldb = nullptr;
        auto status = EncryptedLevelDB::Open(LevelDB::defaultDBOptions(), _name, &pleveldb,
            "85cf52964f7334c015545b394c1ffec9",
            asString(fromHex("3031323334353637303132333435363730313233343536373031323334353637")));
        if (!status.ok())
        {
            LOG(ERROR) << "[g:" << _name << "]"
                       << "Open DB Error" << endl;
            throw;
        }
        return shared_ptr<BasicLevelDB>(pleveldb);
    }

    string randString(size_t _len)
    {
        string res;
        for (size_t i = 0; i < _len; i++)
        {
            res += string(to_string(rand() % 10));
        }
        return res;
    }
};

BOOST_FIXTURE_TEST_SUITE(EncryptedLevelDBTest, EncryptedLevelDBFixture)

BOOST_AUTO_TEST_CASE(OpenTest)
{
    BOOST_CHECK_NO_THROW(shared_ptr<BasicLevelDB> db = openEncryptedDB("./openTestDB"));
}

BOOST_AUTO_TEST_CASE(PutTest)
{
    string k = randString(64);
    string v = randString(256);

    shared_ptr<BasicLevelDB> db = openEncryptedDB("./PutTestDB");
    db->Put(LevelDB::defaultWriteOptions(), Slice(k), Slice(v));

    string* compareVPtr = new string();
    db->Get(LevelDB::defaultReadOptions(), Slice(k), compareVPtr);
    BOOST_CHECK(*compareVPtr == v);
    delete compareVPtr;
}

BOOST_AUTO_TEST_CASE(WriteTest)
{
    vector<string> ks;
    vector<string> vs;
    for (size_t i = 0; i < 10; i++)
    {
        ks.emplace_back(randString(64));
        vs.emplace_back(randString(256));
    }

    shared_ptr<BasicLevelDB> db = openEncryptedDB("./WriteTestDB");
    auto writeBatch = db->createWriteBatch();

    for (size_t i = 0; i < 10; i++)
    {
        writeBatch->insertSlice(Slice(ks[i]), Slice(vs[i]));
    }

    db->Write(LevelDB::defaultWriteOptions(), &writeBatch->writeBatch());

    string* compareVPtr = new string();
    for (size_t i = 0; i < 10; i++)
    {
        db->Get(LevelDB::defaultReadOptions(), Slice(ks[i]), compareVPtr);
        BOOST_CHECK(*compareVPtr == vs[i]);
    }

    delete compareVPtr;
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev