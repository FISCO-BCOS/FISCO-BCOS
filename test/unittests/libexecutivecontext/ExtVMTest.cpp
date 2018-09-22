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
 * @file ExtVMTest.cpp
 * @author: jimmyshi
 * @date 2018-09-21
 */

#include <libdevcore/FixedHash.h>
#include <libexecutivecontext/ExtVM.h>
#include <libexecutivecontext/MptState.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace std;
using namespace dev::eth;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ExtVMTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(stateFaceTest)
{
    OverlayDB db = State::openDB("./", h256("0x1234"));
    MptState s(Invalid256, db, BaseState::Empty);
    ExtVM vm(s);

    Address addr = Address(0);
    bytes code = fromHex("60606060");
    bytes codeBuff = code;  // codeBuff is cleared after setCode
    cout << "code: " << toHex(codeBuff) << endl;
    vm.setCode(addr, std::move(codeBuff));
    bytes codeResult = vm.codeAt(addr);

    cout << "codeBuff: " << toHex(codeBuff) << endl;
    cout << "codeResult: " << toHex(codeResult) << endl;
    BOOST_CHECK(code == codeResult);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev