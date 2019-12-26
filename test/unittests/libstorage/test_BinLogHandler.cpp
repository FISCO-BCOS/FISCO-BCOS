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
/** @file test_BinLogHandler.cpp
 *  @author chaychen
 *  @date 20190802
 */

#include <libstorage/BinLogHandler.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;

namespace test_BinLogHandler
{
struct BinLogHandlerFixture
{
    BinLogHandlerFixture() : binLogHandler(std::make_shared<BinLogHandler>("./binlog/")) {}

    std::shared_ptr<BinLogHandler> binLogHandler;

    ~BinLogHandlerFixture() {}
};
BOOST_FIXTURE_TEST_SUITE(BinLogHandler, BinLogHandlerFixture)
BOOST_AUTO_TEST_CASE(testTableData)
{
    std::vector<TableData::Ptr> oirDatas;
    auto data = std::make_shared<TableData>();
    data->info = std::make_shared<TableInfo>();
    data->info->name = "t_test";
    data->info->key = "key";
    data->info->fields = std::vector<std::string>{"value"};
    data->dirtyEntries = std::make_shared<Entries>();
    auto entry1 = std::make_shared<Entry>();
    entry1->setField("key", "dirtyKey1");
    entry1->setField("value", "dirtyValue1");
    entry1->setID(0);
    data->dirtyEntries->addEntry(entry1);
    data->newEntries = std::make_shared<Entries>();
    auto entry2 = std::make_shared<Entry>();
    entry2->setField("key", "newKey1");
    entry2->setField("value", "newValue1");
    entry2->setID(1);
    data->newEntries->addEntry(entry2);
    oirDatas.push_back(data);

    BOOST_CHECK(binLogHandler->writeBlocktoBinLog(0, oirDatas) == true);
    BOOST_CHECK(binLogHandler->writeBlocktoBinLog(1, oirDatas) == true);
    std::shared_ptr<BlockDataMap> binLogData = binLogHandler->getMissingBlocksFromBinLog(0, 1);
    BOOST_CHECK((*binLogData)[1].size() == 1);
    BOOST_CHECK((*binLogData)[1][0]->info->name == "t_test");
    BOOST_CHECK((*binLogData)[1][0]->info->key == "key");
    BOOST_CHECK((*binLogData)[1][0]->info->fields.size() == 1);
    BOOST_CHECK((*binLogData)[1][0]->info->fields[0] == "value");
    BOOST_CHECK((*binLogData)[1][0]->dirtyEntries->size() == 1);
    BOOST_CHECK((*binLogData)[1][0]->newEntries->size() == 1);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test_BinLogHandler
