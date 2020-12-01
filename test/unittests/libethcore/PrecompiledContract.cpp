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
 * @brief make some ut for PrecompiledContract
 *
 * @file PrecompiledContract.cpp of Ethcore
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libethcore/PrecompiledContract.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::eth;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(EthcorePrecompiledContractTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testEthcorePrecompiledContract)
{
    PrecompiledPricer pricer = PrecompiledRegistrar::pricer("modexp");
    PrecompiledExecutor exec = PrecompiledRegistrar::executor("modexp");
    PrecompiledContract defaultPrecompiledContract(pricer, exec, 0);
    bytes in = *fromHexString(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");

    auto res = defaultPrecompiledContract.cost(bytesConstRef(in.data(), in.size()));
    BOOST_REQUIRE_EQUAL(static_cast<int>(res), 13056);

    in = *fromHexString(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "00"
        "00"
        "00");

    auto res0 = defaultPrecompiledContract.execute(bytesConstRef(in.data(), in.size()));
    BOOST_REQUIRE(res0.first);
    bytes expected =
        *fromHexString("0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        res0.second.begin(), res0.second.end(), expected.begin(), expected.end());

    BOOST_REQUIRE_EQUAL(defaultPrecompiledContract.startingBlock(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
