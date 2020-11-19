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
/** @file test_Condition.cpps
 *  @author monan
 *  @date 20190410
 */

#include "Common.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcrypto/Common.h>
#include <libstorage/Table.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::test;

namespace test_Condition
{
struct ConditionFixture
{
    ConditionFixture()
    {
        entry = std::make_shared<Entry>();
        entry->setField("name", "myname");
        entry->setField("item_id", "100");
        entry->setField("price", boost::lexical_cast<std::string>(100 * 10000));
    }

    Entry::Ptr entry;

    ~ConditionFixture() {}
};

struct SM_ConditionFixture : public ConditionFixture, public SM_CryptoTestFixture
{
    SM_ConditionFixture() : ConditionFixture(), SM_CryptoTestFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(ConditionTest, ConditionFixture)

BOOST_AUTO_TEST_CASE(process)
{
    auto condition = std::make_shared<Condition>();

    condition->EQ("name", "myname");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->EQ("name", "myname2");
    BOOST_TEST(condition->process(entry) == false);

    auto version = g_BCOSConfig.version();
    auto supportedVersion = g_BCOSConfig.supportedVersion();
    g_BCOSConfig.setSupportedVersion("2.3.0", V2_3_0);
    condition = std::make_shared<Condition>();
    condition->NE("name", "myname");
    BOOST_TEST(condition->process(entry) == false);

    condition = std::make_shared<Condition>();
    condition->NE("name", "myname2");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->GT("item_id", "50");
    BOOST_TEST(condition->process(entry) == true);

    condition->GT("item_id", "100");
    BOOST_TEST(condition->process(entry) == false);

    condition->GT("item_id", "150");
    BOOST_TEST(condition->process(entry) == false);

    condition = std::make_shared<Condition>();
    condition->GE("item_id", "50");
    BOOST_TEST(condition->process(entry) == true);

    condition->GE("item_id", "100");
    BOOST_TEST(condition->process(entry) == true);

    condition->GE("item_id", "150");
    BOOST_TEST(condition->process(entry) == false);

    condition = std::make_shared<Condition>();
    condition->LT("item_id", "50");
    BOOST_TEST(condition->process(entry) == false);

    condition->LT("item_id", "100");
    BOOST_TEST(condition->process(entry) == false);

    condition->LT("item_id", "150");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->LE("item_id", "50");
    BOOST_TEST(condition->process(entry) == false);

    condition->LE("item_id", "100");
    BOOST_TEST(condition->process(entry) == true);

    condition->LE("item_id", "150");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->GT("item_id", "99");
    condition->LT("item_id", "101");
    BOOST_TEST(condition->process(entry) == true);

    condition->GE("item_id", "100");
    condition->LT("item_id", "101");
    BOOST_TEST(condition->process(entry) == true);
    g_BCOSConfig.setSupportedVersion(supportedVersion, version);
}

BOOST_FIXTURE_TEST_CASE(SM_process, SM_ConditionFixture)
{
    auto condition = std::make_shared<Condition>();

    condition->EQ("name", "myname");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->EQ("name", "myname2");
    BOOST_TEST(condition->process(entry) == false);

    condition = std::make_shared<Condition>();
    condition->GT("item_id", "50");
    BOOST_TEST(condition->process(entry) == true);

    condition->GT("item_id", "100");
    BOOST_TEST(condition->process(entry) == false);

    condition->GT("item_id", "150");
    BOOST_TEST(condition->process(entry) == false);

    condition = std::make_shared<Condition>();
    condition->GE("item_id", "50");
    BOOST_TEST(condition->process(entry) == true);

    condition->GE("item_id", "100");
    BOOST_TEST(condition->process(entry) == true);

    condition->GE("item_id", "150");
    BOOST_TEST(condition->process(entry) == false);

    condition = std::make_shared<Condition>();
    condition->LT("item_id", "50");
    BOOST_TEST(condition->process(entry) == false);

    condition->LT("item_id", "100");
    BOOST_TEST(condition->process(entry) == false);

    condition->LT("item_id", "150");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->LE("item_id", "50");
    BOOST_TEST(condition->process(entry) == false);

    condition->LE("item_id", "100");
    BOOST_TEST(condition->process(entry) == true);

    condition->LE("item_id", "150");
    BOOST_TEST(condition->process(entry) == true);

    condition = std::make_shared<Condition>();
    condition->GT("item_id", "99");
    condition->LT("item_id", "101");
    BOOST_TEST(condition->process(entry) == true);

    condition->GE("item_id", "100");
    condition->LT("item_id", "101");
    BOOST_TEST(condition->process(entry) == true);
}

BOOST_AUTO_TEST_CASE(greaterThan)
{
#if 0
	auto condition1 = std::make_shared<Condition>();
	auto condition2 = std::make_shared<Condition>();

	condition1->EQ("t", "100");
	condition2->EQ("t", "100");

	BOOST_TEST(condition1->graterThan(condition2) == true);

	condition2->EQ("t", "101");

	BOOST_TEST(condition1->graterThan(condition2) == false);

	condition1 = std::make_shared<Condition>();
	condition2 = std::make_shared<Condition>();

	condition1->GT("t", "100");
	condition2->GT("t", "101");

	BOOST_TEST(condition1->graterThan(condition2) == true);
	BOOST_TEST(condition2->graterThan(condition1) == false);

	condition1->GE("t", "101");

	BOOST_TEST(condition1->graterThan(condition2) == true);
	BOOST_TEST(condition2->graterThan(condition1) == false);

	condition1->LT("t", "101");
	condition2->LT("t", "100");

	BOOST_TEST(condition1->graterThan(condition2) == true);
	BOOST_TEST(condition2->graterThan(condition1) == false);

	condition1->LE("t", "101");

	BOOST_TEST(condition1->graterThan(condition2) == true);
	BOOST_TEST(condition2->graterThan(condition1) == false);
#endif
}

BOOST_AUTO_TEST_CASE(related)
{
#if 0
	auto condition1 = std::make_shared<Condition>();
	auto condition2 = std::make_shared<Condition>();

	condition1->EQ("t", "100");
	condition2->EQ("t", "100");
	BOOST_TEST(condition1->related(condition2) == true);

	condition2->EQ("t", "101");

	BOOST_TEST(condition1->related(condition2) == false);

	condition1 = std::make_shared<Condition>();
	condition2 = std::make_shared<Condition>();

	condition1->GT("t", "100");
	condition2->GT("t", "101");

	BOOST_TEST(condition1->related(condition2) == true);
	BOOST_TEST(condition2->related(condition1) == true);

	condition1->GE("t", "101");

	BOOST_TEST(condition1->related(condition2) == true);
	BOOST_TEST(condition2->related(condition1) == true);

	condition1->LT("t", "101");
	condition2->LT("t", "100");

	BOOST_TEST(condition1->related(condition2) == true);
	BOOST_TEST(condition2->related(condition1) == true);

	condition1->LE("t", "101");

	BOOST_TEST(condition1->related(condition2) == true);
	BOOST_TEST(condition2->related(condition1) == true);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Condition
