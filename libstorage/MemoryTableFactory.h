/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file MemoryTableFactory.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Storage.h"
#include "Table.h"

namespace dev
{
namespace storage
{
class MemoryTableFactory : public StateDBFactory
{
public:
    typedef std::shared_ptr<MemoryTableFactory> Ptr;

    virtual ~MemoryTableFactory() {}

    Table::Ptr openTable(h256 blockHash, int num, const std::string& table) override;
    Table::Ptr createTable(h256 blockHash, int num, const std::string& tableName,
        const std::string& keyField, const std::vector<std::string>& valueField) override;

    virtual Storage::Ptr stateStorage() { return m_stateStorage; }
    virtual void setStateStorage(Storage::Ptr stateStorage) { m_stateStorage = stateStorage; }

    void setBlockHash(h256 blockHash);
    void setBlockNum(int blockNum);

private:
    Storage::Ptr m_stateStorage;
    h256 m_blockHash;
    int m_blockNum;
};

}  // namespace storage

}  // namespace dev
