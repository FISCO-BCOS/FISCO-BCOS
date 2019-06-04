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
 * @brief : Encrypted file unitest
 * @author: jimmyshi
 * @date: 2019-05-16
 */

#include <leveldb/db.h>
#include <leveldb/slice.h>
#include <leveldb/status.h>
#include <libdevcore/BasicLevelDB.h>
#include <libdevcore/LevelDB.h>
#include <libsecurity/EncryptedFile.h>
#include <libsecurity/KeyCenter.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>


using namespace std;
using namespace dev;
using namespace dev::db;

namespace dev
{
namespace test
{
class FakeKeyCenter : public KeyCenter
{
    const dev::bytes getDataKey(const std::string&) override
    {
        bytes readableDataKey = fromHex("313233343536");
        return uniformDataKey(readableDataKey);
    }
};

class EncryptedFileFixture : public TestOutputHelperFixture
{
public:
    const string plainText =
        "2d2d2d2d2d424547494e2050524956415445204b45592d2d2d2d2d0a4d494745416745414d4241474279714753"
        "4d343941674547425375424241414b4247307761774942415151674a476d6d3952494b71315a764d4177487737"
        "74350a69724245766e6956706a444f497242785a4f626d3772696852414e434141516b457a3732637563386745"
        "51674f6d4e41716848687557477a5952732b447842780a52394c6b576d6c694561684a48714b64774451317879"
        "577a5379306748676543527268746456316b3235516d67784479424371700a2d2d2d2d2d454e44205052495641"
        "5445204b45592d2d2d2d2d0a";
    EncryptedFileFixture() : TestOutputHelperFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(EncryptedFileTest, EncryptedFileFixture)

BOOST_AUTO_TEST_CASE(decryptContentsTest)
{
    shared_ptr<KeyCenter> fakeKeyCenter = make_shared<FakeKeyCenter>();
#ifdef FISCO_GM
    bytes res = EncryptedFile::decryptContents(
        getTestPath().string() + "/fisco-bcos-data/encgmnode.key", fakeKeyCenter);
#else
    bytes res = EncryptedFile::decryptContents(
        getTestPath().string() + "/fisco-bcos-data/encnode.key", fakeKeyCenter);
#endif
    LOG(DEBUG) << toHex(res);
    BOOST_CHECK(plainText == toHex(res));
}


BOOST_AUTO_TEST_SUITE_END()


}  // namespace test
}  // namespace dev