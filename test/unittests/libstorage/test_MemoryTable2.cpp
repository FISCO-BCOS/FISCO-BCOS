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

#include "MemoryStorage2.h"
#include <libdevcore/FixedHash.h>
#include <libstorage/Common.h>
#include <libstorage/MemoryTable2.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <tbb/parallel_for.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::storage;

namespace test_MemoryTableFactory2
{
struct MemoryTable2Fixture
{
    MemoryTable2Fixture()
    {
        auto storage = std::make_shared<MemoryStorage2>();
        m_table = std::make_shared<MemoryTable2>();
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        info->fields.emplace_back(ID_FIELD);
        info->fields.emplace_back("name");
        info->fields.emplace_back(STATUS);
        info->key = "name";
        info->fields = vector<string>{"value"};
        info->name = "test_memtable2";
        m_table->setTableInfo(info);
        m_table->setRecorder(
            [&](Table::Ptr, Change::Kind, string const&, vector<Change::Record>&) {});
    }

    dev::storage::MemoryTable2::Ptr m_table;
};

BOOST_FIXTURE_TEST_SUITE(MemoryTable2, MemoryTable2Fixture)

BOOST_AUTO_TEST_CASE(update_select)
{
    auto memStorage = std::make_shared<MemoryStorage2>();
    TableData::Ptr tableData = std::make_shared<TableData>();
    Entries::Ptr insertEntries = std::make_shared<Entries>();
    // insert
    auto entry = std::make_shared<storage::Entry>();
    entry->setField("name", "WangWu");
    entry->setField("value", "0.5");
    entry->setID(1);
    insertEntries->addEntry(entry);
    tableData->newEntries = insertEntries;
    tableData->info = m_table->tableInfo();
    memStorage->commit(1, vector<TableData::Ptr>{tableData});

    m_table->setStateStorage(memStorage);
    auto condition = std::make_shared<storage::Condition>();
    auto entries = m_table->select(std::string("WangWu"), condition);
    BOOST_TEST(entries->size() == 1u);
    // update
    condition = std::make_shared<storage::Condition>();
    condition->EQ("name", "WangWu");
    entry = std::make_shared<storage::Entry>();
    entry->setField("value", "0.6");
    auto ret = m_table->update(std::string("WangWu"), entry, condition);
    BOOST_TEST(ret == 1);

    auto m_version = g_BCOSConfig.version();
    auto m_supportedVersion = g_BCOSConfig.supportedVersion();
    g_BCOSConfig.setSupportedVersion("2.4.0", V2_4_0);
    // select 0.6 should success
    condition = std::make_shared<storage::Condition>();
    condition->EQ("value", "0.6");
    entries = m_table->select(std::string("WangWu"), condition);
    BOOST_TEST(entries->size() == 0);
    // select 0.5 should failed
    condition = std::make_shared<storage::Condition>();
    condition->EQ("value", "0.5");
    entries = m_table->select(std::string("WangWu"), condition);
    BOOST_TEST(entries->size() == 1u);

    g_BCOSConfig.setSupportedVersion("2.5.0", V2_5_0);
    condition = std::make_shared<storage::Condition>();
    condition->EQ("value", "0.6");
    entries = m_table->select(std::string("WangWu"), condition);
    BOOST_TEST(entries->size() == 1u);
    condition = std::make_shared<storage::Condition>();
    condition->EQ("value", "0.5");
    entries = m_table->select(std::string("WangWu"), condition);
    BOOST_TEST(entries->size() == 0);
    g_BCOSConfig.setSupportedVersion(m_supportedVersion, m_version);
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_MemoryTableFactory2
