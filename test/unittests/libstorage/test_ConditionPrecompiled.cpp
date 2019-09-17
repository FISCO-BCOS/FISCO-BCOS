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

#include "Common.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ConditionPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;

namespace test_ConditionPrecompiled
{
class MockPrecompiledEngine : public dev::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

struct ConditionPrecompiledFixture
{
    ConditionPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        conditionPrecompiled = std::make_shared<dev::blockverifier::ConditionPrecompiled>();
        auto condition = std::make_shared<storage::Condition>();
        conditionPrecompiled->setPrecompiledEngine(context);
        conditionPrecompiled->setCondition(condition);
    }

    ~ConditionPrecompiledFixture() {}
    dev::blockverifier::ConditionPrecompiled::Ptr conditionPrecompiled;
    ExecutiveContext::Ptr context;
};

BOOST_FIXTURE_TEST_SUITE(ConditionPrecompiled, ConditionPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_CHECK_EQUAL((conditionPrecompiled->toString() == "Condition"), true);
}

BOOST_AUTO_TEST_CASE(getCondition)
{
    conditionPrecompiled->getCondition();
}

BOOST_AUTO_TEST_CASE(call)
{
    eth::ContractABI abi;

    bytes in = abi.abiIn("EQ(string,int256)", std::string("price"), s256(256));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("EQ(string,string)", std::string("item"), std::string("spaceship"));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("GE(string,int256)", std::string("price"), s256(256));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("GT(string,int256)", std::string("price"), s256(256));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("LE(string,int256)", std::string("price"), s256(256));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("LT(string,int256)", std::string("price"), s256(256));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("NE(string,int256)", std::string("price"), s256(256));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("NE(string,string)", std::string("name"), std::string("WangWu"));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("limit(int256)", u256(123));
    conditionPrecompiled->call(context, bytesConstRef(&in));
    in = abi.abiIn("limit(int256,int256)", u256(2), u256(3));
    conditionPrecompiled->call(context, bytesConstRef(&in));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_ConditionPrecompiled
