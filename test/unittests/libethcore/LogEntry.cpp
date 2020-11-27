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
 * @brief
 *
 * @file LogEntry.cpp
 * @author: jimmyshi
 * @date 2018-08-31
 */

#include <libethcore/LogEntry.h>
#include <libutilities/RLP.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace std;
using namespace bcos;
using namespace bcos::eth;
using namespace bcos::test;
namespace ut = boost::unit_test;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(LogEntryTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(LogEntryBasic)
{
    Address address("ccdeac59d35627b7de09332e819d5159e7bb7250");
    h256 t1("0x12b5155eda010a5b7ae26a4a268e466a4b8d31547ad875fce9ab298c639a1b2f");
    h256 t2("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    h256s topics = {t1, t2};
    bytes data({'a', 'b', '5', '6'});

    LogEntry le = LogEntry(address, topics, data);
    BOOST_CHECK(le.address == address);
    BOOST_CHECK(le.topics == topics);
    BOOST_CHECK(le.data == data);

    RLPStream s;
    le.streamRLP(s);
    RLP r(&s.out());
    LogEntry compareLe(r);

    BOOST_CHECK(le.address == compareLe.address);
    BOOST_CHECK(le.topics == compareLe.topics);
    BOOST_CHECK(le.data == compareLe.data);
}

BOOST_AUTO_TEST_CASE(BloomTest)
{
    Address address("ccdeac59d35627b7de09332e819d5159e7bb7250");
    h256 t1("0x12b5155eda010a5b7ae26a4a268e466a4b8d31547ad875fce9ab298c639a1b2f");
    h256 t2("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    h256s topics = {t1, t2};
    bytes data({'a', 'b', '5', '6'});

    LogEntry le = LogEntry(address, topics, data);
    LogBloom bl = le.bloom();
    std::cout << "LogBloom: " << bl.hex() << std::endl;
    std::string compareLb(
        "000000000000004000000000000000000000000000140000000000000004000000000400000000000000000000"
        "000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000080000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00000080000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(bl.hex(), compareLb);
    // BOOST_CHECK(bl.hex() == compareLb);
}

BOOST_FIXTURE_TEST_CASE(SM_BloomTest, SM_CryptoTestFixture)
{
    Address address("ccdeac59d35627b7de09332e819d5159e7bb7250");
    h256 t1("0x12b5155eda010a5b7ae26a4a268e466a4b8d31547ad875fce9ab298c639a1b2f");
    h256 t2("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    h256s topics = {t1, t2};
    bytes data({'a', 'b', '5', '6'});

    LogEntry le = LogEntry(address, topics, data);
    LogBloom bl = le.bloom();
    std::cout << "LogBloom: " << bl.hex() << std::endl;
    std::string compareLb(
        "000000000000000000000000000080000000000000000000000000000008040000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000040000002000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000"
        "000000000000020000000000000000000000080000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000000000000000000000200000000000");
    BOOST_CHECK_EQUAL(bl.hex(), compareLb);
    // BOOST_CHECK(bl.hex() == compareLb);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
