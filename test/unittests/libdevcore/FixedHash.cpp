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
 * @brief Unit tests for the FixedHash
 * @file FixedHash.cpp
 * @author: chaychen
 * @date 2018
 */

#include <libdevcore/CommonData.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/FixedHash.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FixedHash, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testFromUUID)
{
    const std::string uuid = "067150c0-7dab-4fac-b716-0e075548007e";
    h128 h = fromUUID(uuid);
    BOOST_CHECK("0x067150c07dab4facb7160e075548007e" == toJS(h));

    const std::string uuidError = "067150c0+7dab-4fac-b716-0e075548007e";
    BOOST_CHECK(h128() == fromUUID(uuidError));
}

BOOST_AUTO_TEST_CASE(testToUUID)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e";
    h128 h = jsToFixed<16>(str);
    BOOST_CHECK("067150c0-7dab-4fac-b716-0e075548007e" == toUUID(h));
}

BOOST_AUTO_TEST_CASE(testLeft160)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    h256 h = jsToFixed<32>(str);
    BOOST_CHECK("0x067150c07dab4facb7160e075548007e067150c0" == toJS(left160(h)));
}

BOOST_AUTO_TEST_CASE(testRight160)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    h256 h = jsToFixed<32>(str);
    BOOST_CHECK("0x5548007e067150c07dab4facb7160e075548007e" == toJS(right160(h)));
    // test u256
    h256 h256Data(fromHex("12b5155eda010a5b7ae26a4a268e466a4b8d31547ad875fce9ab298c639a1b2f"));
    // trans h256Data to u256
    u256 value(h256Data);
    // trans value to h256 again
    h256 convertedH256Data = value;
    std::cout << "### value: " << value << ", h256Data:" << dev::toHex(h256Data)
              << "convertedH256Data" << dev::toHex(convertedH256Data) << std::endl;
    BOOST_CHECK(convertedH256Data == h256Data);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
