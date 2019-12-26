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

#pragma once

#include "Storage.h"
#include <atomic>

namespace dev
{
namespace storage
{
class ScalableStorage : public Storage
{
public:
    typedef std::shared_ptr<ScalableStorage> Ptr;
    explicit ScalableStorage(int64_t _scrollThreshold);

    virtual ~ScalableStorage();

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;

    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override;

    void setRemoteStorage(Storage::Ptr _remote) { m_remote = _remote; }
    void setStateStorage(Storage::Ptr _state) { m_state = _state; }
    void setArchiveStorage(Storage::Ptr _archive, int64_t _dbIndex)
    {
        m_archive = _archive;
        m_archiveDBName = _dbIndex;
    }
    void setStorageFactory(StorageFactory::Ptr _storageFactory)
    {
        m_storageFactory = _storageFactory;
    }
    int64_t setRemoteBlockNumber(int64_t _blockNumber);
    int64_t getRemoteBlockNumber() { return m_remoteBlockNumber.load(); }
    void stop() override;
    std::string getDBNameOfArchivedBlock(int64_t _blockNumber);

    bool isStateData(const std::string& _tableName);

private:
    Entries::Ptr selectFromArchive(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr);
    void separateData(const std::vector<TableData::Ptr>& datas,
        std::vector<TableData::Ptr>& stateData, std::vector<TableData::Ptr>& archiveData);
    TableData::Ptr getNumberToDBNameData(int64_t _blockNumber);
    Storage::Ptr m_remote = nullptr;
    Storage::Ptr m_state = nullptr;
    Storage::Ptr m_archive = nullptr;
    int64_t m_archiveDBName = -1;  ///< also the start block of DB
    std::mutex m_archiveMutex;
    StorageFactory::Ptr m_storageFactory = nullptr;
    const int64_t m_scrollThreshold;
    std::atomic<int64_t> m_remoteBlockNumber;
    const std::vector<std::string> m_archiveTables = {SYS_HASH_2_BLOCK, SYS_BLOCK_2_NONCES};
};
}  // namespace storage
}  // namespace dev
