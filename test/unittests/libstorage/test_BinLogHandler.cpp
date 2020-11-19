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

using namespace bcos;
using namespace bcos::storage;

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
    data->info->fields = std::vector<std::string>{
        "value1",
        "value2",
        "value3",
        "value4",
        "value5",
        "value6",
        "value7",
        "value8",
        "value9",
        "value10",
        "value11",
        "value12",
    };
    data->dirtyEntries = std::make_shared<Entries>();
    auto entry1 = std::make_shared<Entry>();
    entry1->setField("key", "dirtyKey1");
    entry1->setField("value1", "dirtyValue1");
    entry1->setField("value2", "dirtyValue2");
    entry1->setField("value3", "dirtyValue3");
    entry1->setField("value4", "dirtyValue4");
    entry1->setField("value5", "dirtyValue5");
    entry1->setField("value6", "dirtyValue6");
    entry1->setField("value7", "dirtyValue7");
    entry1->setField("value8", "dirtyValue8");
    entry1->setField("value9", "dirtyValue9");
    entry1->setField("value10", "dirtyValue10");
    entry1->setField("value11", "dirtyValue11");
    entry1->setField("value12", "dirtyValue12");
    entry1->setID(0);
    data->dirtyEntries->addEntry(entry1);
    data->newEntries = std::make_shared<Entries>();
    auto entry2 = std::make_shared<Entry>();
    entry2->setField("key", "newKey1");
    entry2->setField("value1", "newValue1");
    entry2->setField("value3", "newValue3");
    entry2->setField("value6", "newValue6");
    entry2->setID(1);
    data->newEntries->addEntry(entry2);
    auto entry3 = std::make_shared<Entry>();
    entry3->setField("key", "newKey2");
    entry3->setField("value2", "newValue2");
    entry3->setField("value4", "newValue4");
    entry3->setField("value5", "newValue5");
    entry3->setField("value6", "newValue6");
    entry3->setField("value8", "newValue8");
    entry3->setField("value10", "newValue10");
    entry3->setField("value12", "newValue12");
    entry3->setID(2);
    data->newEntries->addEntry(entry3);
    oirDatas.push_back(data);

    BOOST_CHECK(binLogHandler->writeBlocktoBinLog(0, oirDatas) == true);
    BOOST_CHECK(binLogHandler->writeBlocktoBinLog(1, oirDatas) == true);
    std::shared_ptr<BlockDataMap> binLogData = binLogHandler->getMissingBlocksFromBinLog(0, 1);
    BOOST_CHECK((*binLogData)[1].size() == 1);
    BOOST_CHECK((*binLogData)[1][0]->info->name == "t_test");
    BOOST_CHECK((*binLogData)[1][0]->info->key == "key");
    BOOST_CHECK((*binLogData)[1][0]->info->fields.size() == 12);
    BOOST_CHECK((*binLogData)[1][0]->info->fields[0] == "value1");
    BOOST_CHECK((*binLogData)[1][0]->info->fields[1] == "value2");
    BOOST_CHECK((*binLogData)[1][0]->dirtyEntries->size() == 1);
    BOOST_CHECK((*binLogData)[1][0]->newEntries->size() == 2);

    Entry::Ptr entry = (*binLogData)[1][0]->newEntries->get(1);
    BOOST_CHECK(entry->getField("value2") == "newValue2");
    BOOST_CHECK(entry->getField("value4") == "newValue4");
    BOOST_CHECK(entry->getField("value5") == "newValue5");
    BOOST_CHECK(entry->getField("value6") == "newValue6");
    BOOST_CHECK(entry->getField("value8") == "newValue8");
    BOOST_CHECK(entry->getField("value10") == "newValue10");
    BOOST_CHECK(entry->getField("value12") == "newValue12");
    BOOST_CHECK(entry->find("value1") == entry->end());
    BOOST_CHECK(entry->find("value3") == entry->end());
    BOOST_CHECK(entry->find("value7") == entry->end());
    BOOST_CHECK(entry->find("value9") == entry->end());
    BOOST_CHECK(entry->find("value11") == entry->end());
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test_BinLogHandler
