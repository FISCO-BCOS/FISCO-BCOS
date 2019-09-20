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
 * (c) 2016-2020 fisco-dev contributors.
 */
/** @file ScalableStorage.h
 *  @author xingqiangbai
 *  @date 20190917
 */

#include "ScalableStorage.h"
#include "StorageException.h"
#include "libdevcore/Common.h"
#include "libdevcore/Guards.h"
#include "libdevcore/Log.h"
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <mutex>

using namespace std;
using namespace boost;
using namespace dev;
using namespace dev::storage;

const char* const SYS_REMOTE_BLOCK_NUMBER = "remote_block_number";

ScalableStorage::ScalableStorage(int64_t _scrollThreshold)
  : m_scrollThreshold(_scrollThreshold), m_remoteBlockNumber(-1)
{}

ScalableStorage::~ScalableStorage()
{
    stop();
}

void ScalableStorage::init()
{
    TableInfo::Ptr tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = SYS_CURRENT_STATE;
    auto entries = m_state->select(0, tableInfo, SYS_REMOTE_BLOCK_NUMBER, nullptr);
    if (entries && entries->size() > 0)
    {
        auto entry = entries->get(0);
        std::string remoteNumber = entry->getField(SYS_VALUE);
        m_remoteBlockNumber = lexical_cast<int64_t>(remoteNumber);
    }
}

int64_t ScalableStorage::setRemoteBlockNumber(int64_t _blockNumber)
{
    if (_blockNumber <= m_remoteBlockNumber.load())
    {
        return m_remoteBlockNumber.load();
    }
    int64_t newNumber = _blockNumber - _blockNumber % m_scrollThreshold;
    m_remoteBlockNumber.store(newNumber);
    return newNumber;
}

bool ScalableStorage::isStateData(const std::string& _tableName)
{
    return m_archiveTables.end() ==
           find(m_archiveTables.begin(), m_archiveTables.end(), _tableName);
}

Entries::Ptr ScalableStorage::selectFromArchive(
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    int64_t dbIndex = num / m_scrollThreshold;
    assert(dbIndex >= 0);
    Guard l(m_archiveMutex);
    SCALABLE_STORAGE_LOG(DEBUG) << "select from archive DB" << LOG_KV("dbIndex", dbIndex)
                                << LOG_KV("currentIndex", m_archiveDBIndex);
    if (dbIndex == m_archiveDBIndex)
    {
        return m_archive->select(num, tableInfo, key, condition);
    }
    else
    {
        auto dataStorage = m_storageFactory->getStorage(to_string(dbIndex));
        return dataStorage->select(num, tableInfo, key, condition);
    }
    return nullptr;
}

Entries::Ptr ScalableStorage::select(
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    if (isStateData(tableInfo->name))
    {
        return m_state->select(num, tableInfo, key, condition);
    }
    else
    {
        if (num < m_remoteBlockNumber.load() && m_remote)
        {
            SCALABLE_STORAGE_LOG(DEBUG)
                << "select from remote" << LOG_KV("table", tableInfo->name) << LOG_KV("key", key);
            return m_remote->select(num, tableInfo, key, condition);
        }
        else if (m_archive)
        {
            return selectFromArchive(num, tableInfo, key, condition);
        }
    }
    SCALABLE_STORAGE_LOG(FATAL) << "No backend storage, please check!";
    return nullptr;
}

void ScalableStorage::separateData(const std::vector<TableData::Ptr>& datas,
    std::vector<TableData::Ptr>& stateData, std::vector<TableData::Ptr>& archiveData)
{
    for (auto const& tableData : datas)
    {
        if (isStateData(tableData->info->name))
        {
            stateData.emplace_back(tableData);
        }
        else
        {
            archiveData.emplace_back(tableData);
        }
    }
}

void ScalableStorage::writeRemoteBlockNumber(
    const std::vector<TableData::Ptr>& datas, int64_t _blockNumber, int64_t _remoteNumber)
{
    auto currentState = find_if(datas.begin(), datas.end(), [](TableData::Ptr tableData) -> bool {
        return tableData->info->name == SYS_CURRENT_STATE;
    });
    Entry::Ptr idEntry = std::make_shared<Entry>();
    idEntry->setNum(_blockNumber);
    idEntry->setStatus(0);
    idEntry->setField(SYS_KEY, SYS_REMOTE_BLOCK_NUMBER);
    idEntry->setField(SYS_VALUE, boost::lexical_cast<std::string>(_remoteNumber));
    (*currentState)->newEntries->addEntry(idEntry);
}


size_t ScalableStorage::commit(int64_t num, const std::vector<TableData::Ptr>& datas)
{
    SCALABLE_STORAGE_LOG(DEBUG) << "ScalableStorage commit" << LOG_KV("size", datas.size())
                                << LOG_KV("block", num) << LOG_KV("dbIndex", m_archiveDBIndex);
    std::vector<TableData::Ptr> archiveData;
    archiveData.reserve(m_archiveTables.size());
    std::vector<TableData::Ptr> stateData;
    stateData.reserve(datas.size() - m_archiveTables.size());
    separateData(datas, stateData, archiveData);
    size_t size = 0;
    writeRemoteBlockNumber(stateData, num, m_remoteBlockNumber);
    size += m_archive->commit(num, archiveData);
    size += m_state->commit(num, stateData);
    if ((num + 1) % m_scrollThreshold == 0)
    {
        Guard l(m_archiveMutex);
        m_archiveDBIndex += 1;
        m_archive = m_storageFactory->getStorage(to_string(m_archiveDBIndex));
        SCALABLE_STORAGE_LOG(DEBUG)
            << "create new Storage" << LOG_KV("block", num) << LOG_KV("dbIndex", m_archiveDBIndex);
    }
    return size;
}

void ScalableStorage::stop()
{
    if (m_remote)
    {
        m_remote->stop();
    }
    SCALABLE_STORAGE_LOG(INFO) << "ScalableStorage stopped";
}
