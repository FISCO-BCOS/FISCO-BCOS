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


#define ZdbStorage_LOG(LEVEL) LOG(LEVEL) << "[ZdbStorage] "

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

    Entries::Ptr select(int64_t _num, TableInfo::Ptr _tableInfo, const std::string& _key,
        Condition::Ptr _condition = nullptr) override;
    size_t commit(int64_t _num, const std::vector<TableData::Ptr>& _datas) override;
    bool onlyCommitDirty() override { return true; }

    virtual void setFatalHandler(std::function<void(std::exception&)> fatalHandler)
    {
        m_fatalHandler = fatalHandler;
    }


public:
    void setConnPool(std::shared_ptr<SQLConnectionPool>& _connPool);
    void SetSqlAccess(SQLBasicAccess::Ptr _sqlBasicAcc);
    void initSysTables();
    void setMaxRetry(int maxRetry) { m_maxRetry = maxRetry; }

private:
    std::function<void(std::exception&)> m_fatalHandler;
    SQLBasicAccess::Ptr m_sqlBasicAcc;


private:
    std::string getCommonFileds();
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

    // create blob table
    void createBlobHash2BlockTables();
    void createBlobSysBlock2NoncesTables();
    void createBlobSysHash2BlockHeaderTable();

    int m_maxRetry = 60;
};

}  // namespace storage

}  // namespace dev
