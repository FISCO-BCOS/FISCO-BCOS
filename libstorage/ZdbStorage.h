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
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file ZdbStorage.h
 *  @author darrenyin
 *  @date 2019-04-24
 */

#pragma once

#include "SQLBasicAccess.h"
#include "Storage.h"
#include <json/json.h>


namespace dev
{
namespace storage
{
class ZdbStorage : public Storage
{
public:
    typedef std::shared_ptr<ZdbStorage> Ptr;

    ZdbStorage();
    virtual ~ZdbStorage(){};

    Entries::Ptr select(h256 _hash, int _num, TableInfo::Ptr _tableInfo, const std::string& _key,
        Condition::Ptr _condition = nullptr) override;
    size_t commit(h256 _hash, int64_t _num, const std::vector<TableData::Ptr>& _datas) override;
    bool onlyDirty() override;

public:
    void initSqlAccess(const ZDBConfig& _dbConfig);

private:
    SQLBasicAccess m_sqlBasicAcc;

private:
    void initSysTables();
    void createSysTables();
    void createSysConsensus();
    void createAccessTables();
    void createCurrentStateTables();
    void createNumber2HashTables();
    void createTxHash2BlockTables();
    void createHash2BlockTables();
    void createCnsTables();
    void createSysConfigTables();
    void createSysBlock2NoncesTables();
    void insertSysTables();
};

}  // namespace storage

}  // namespace dev
