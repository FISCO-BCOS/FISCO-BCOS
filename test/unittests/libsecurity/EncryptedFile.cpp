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
#ifdef FISCO_GM
    const string cipherText =
        "f00f824cc6709fe7bb86e620fae4573e12b702f310425f54590445efede0b75dea6c9ac9002f52da9f13b7c415"
        "fe3fbb7754e5469ceb74a2ac2cc121af249580e13fb6912bc06b5c4a5b3db893cce488e6949a759aebf7463e01"
        "5722b59829f5a2702e4a7ce76262165c63bcd7f6842e1dad024be6ad590491ff78cf6b470770e71f10b1f2ad20"
        "bbbf8149aca3c28b22632adc0a867a5b898bf2bae6e5646fdfb3e307063387df15b788b729121727a00c276117"
        "dd8be137177cdf212bbd82a98c0a489746971fdacf64ef7ee7c28a11ef3592c507d024ee01f44fd551108e4345"
        "7479fda8a49fa5488408993ef13c5562168572634647296592d6a831c72e3a9b6b32c5083d73ec8caa9d22e893"
        "39d3992196101e14964e9f60bcb42174baaef70c4c3f2a0c9cf55fd144e9ed43c9060cc8a20394a13948b2a4aa"
        "f770f8d64b";
#else
    const string cipherText =
        "8b2eba71821a5eb15b0cbe710e96f23191419784f644389c58e823477cf33bd73a51b6f14af368d4d3ed647d9d"
        "e6818938ded7b821394446279490b537d04e7a7e87308b66fc82ab3987fb9f3c7079c2477ed4edaf7060ae151f"
        "237b466e4f3fa2eaac8e59cbd36a8a33c6cae2e87c2b98c98c9aa29407e351ed906b973c4484d40bdbd0ba909f"
        "4d6da7c613a3f273c97eabf73c09c741658068cff9941f0f2f8f268855779f0386391c8679db763a7f04a44a07"
        "febc9e3071415d7a0b2037e24435f7973533ce0322284d5532d7a2338804b833df9ad9b06be5eb0e5ca3a54c87"
        "3d32ff0b99cf5898c22c396af76e8babe8429db6b44a72dacdb12c5138095e48e257b9bc4da63787b25a13bbe1"
        "7f1b0204c18c88a035dd5d368eb7586e997f78c0c73b03a50605539abff197a6f52a76a7dd0504d5d9f6aa1aa7"
        "d13af8e16d";
#endif

    EncryptedFileFixture() : TestOutputHelperFixture() {}

    inline void saveStringFile(const string& path, const std::string& str)
    {
        std::ofstream file(path);
        file << str;
        file.close();
    }
};

BOOST_FIXTURE_TEST_SUITE(EncryptedFileTest, EncryptedFileFixture)

BOOST_AUTO_TEST_CASE(decryptContentsTest)
{
    saveStringFile(".test_cipher_file.txt", cipherText);

    shared_ptr<KeyCenter> fakeKeyCenter = make_shared<FakeKeyCenter>();

    bytes res = EncryptedFile::decryptContents(".test_cipher_file.txt", fakeKeyCenter);

    LOG(DEBUG) << toHex(res);
    BOOST_CHECK(plainText == toHex(res));
}


BOOST_AUTO_TEST_SUITE_END()


}  // namespace test
}  // namespace dev