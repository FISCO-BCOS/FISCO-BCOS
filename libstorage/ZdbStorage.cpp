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
/** @file ZdbStorage.cpp
 *  @author darrenyin
 *  @date 2019-04-24
 */

#include "ZdbStorage.h"
#include "Common.h"
#include "StorageException.h"
#include "Table.h"
#include <libdevcore/FixedHash.h>
#include <thread>


using namespace std;
using namespace dev;
using namespace dev::storage;

ZdbStorage::ZdbStorage() {}

Entries::Ptr ZdbStorage::select(
    int64_t _num, TableInfo::Ptr _tableInfo, const std::string& _key, Condition::Ptr _condition)
{
    std::vector<std::map<std::string, std::string> > values;
    int ret = 0, i = 0;
    for (i = 0; i < m_maxRetry; ++i)
    {
        ret = m_sqlBasicAcc->Select(_num, _tableInfo->name, _key, _condition, values);
        if (ret < 0)
        {
            ZdbStorage_LOG(ERROR) << "Remote select datdbase return error:" << ret
                                  << " table:" << _tableInfo->name << LOG_KV("retry", i + 1);
            this_thread::sleep_for(chrono::milliseconds(1000));
            continue;
        }
        else
        {
            break;
        }
    }
    if (i == m_maxRetry && ret < 0)
    {
        ZdbStorage_LOG(ERROR) << "MySQL select return error: " << ret
                              << LOG_KV("table", _tableInfo->name) << LOG_KV("retry", m_maxRetry);
        auto e = StorageException(
            -1, "MySQL select return error:" + to_string(ret) + " table:" + _tableInfo->name);
        m_fatalHandler(e);
        BOOST_THROW_EXCEPTION(e);
    }

    Entries::Ptr entries = std::make_shared<Entries>();
    for (auto it : values)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        for (auto it2 : it)
        {
            if (it2.first == ID_FIELD)
            {
                entry->setID(it2.second);
            }
            else if (it2.first == NUM_FIELD)
            {
                entry->setNum(it2.second);
            }
            else if (it2.first == STATUS)
            {
                entry->setStatus(it2.second);
            }
            else
            {
                entry->setField(it2.first, it2.second);
            }
        }

        if (entry->getStatus() == 0)
        {
            entry->setDirty(false);
            entries->addEntry(entry);
        }
    }
    entries->setDirty(false);
    return entries;
}

void ZdbStorage::setConnPool(std::shared_ptr<SQLConnectionPool>& _connPool)
{
    m_sqlBasicAcc->setConnPool(_connPool);
    this->initSysTables();
}

void ZdbStorage::SetSqlAccess(SQLBasicAccess::Ptr _sqlBasicAcc)
{
    m_sqlBasicAcc = _sqlBasicAcc;
}

size_t ZdbStorage::commit(int64_t _num, const std::vector<TableData::Ptr>& _datas)
{
    int32_t ret = 0;
    for (int i = 0; i < m_maxRetry; ++i)
    {
        ret = m_sqlBasicAcc->Commit((int32_t)_num, _datas);
        if (ret < 0)
        {
            ZdbStorage_LOG(ERROR) << "MySQL commit return error:" << ret << LOG_KV("retry", i + 1);
            this_thread::sleep_for(chrono::milliseconds(1000));
            continue;
        }
        else
        {
            return ret;
        }
    }
    ZdbStorage_LOG(ERROR) << "MySQL commit failed:" << ret << LOG_KV("retry", m_maxRetry);

    auto e = StorageException(-1, "MySQL commit return error:" + to_string(ret));
    m_fatalHandler(e);
    BOOST_THROW_EXCEPTION(e);
    return ret;
}

/*
    init system tables;
*/
void ZdbStorage::initSysTables()
{
    createSysTables();
    createSysConsensus();
    createAccessTables();
    createCurrentStateTables();
    createNumber2HashTables();
    createTxHash2BlockTables();

    createCnsTables();
    createSysConfigTables();
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        // the compressed table include:
        // _sys_hash_2_block, _sys_block_2_nonce_ and _sys_hash_2_header_
        m_rowFormat = " ROW_FORMAT=COMPRESSED ";
        m_valueFieldType = "longblob";
    }
    createHash2BlockTables();
    createSysBlock2NoncesTables();

    createBlobSysHash2BlockHeaderTable();
    insertSysTables();
}

std::string ZdbStorage::getCommonFileds()
{
    string commonFields(
        " `_id_` BIGINT(10) unsigned NOT NULL AUTO_INCREMENT,\n"
        " `_num_` BIGINT(11) DEFAULT '0',\n"
        " `_status_` int(11) DEFAULT '0',\n");
    if (g_BCOSConfig.version() <= V2_1_0)
    {
        commonFields += "`_hash_` varchar(128) DEFAULT NULL,\n";
    }
    return commonFields;
}

void ZdbStorage::createSysTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_TABLES << "` (\n";
    ss << getCommonFileds();
    ss << "`table_name` varchar(128) DEFAULT '',\n";
    ss << "`key_field` varchar(1024) DEFAULT '',\n";
    ss << " `value_field` varchar(1024) DEFAULT '',\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << " UNIQUE KEY `table_name` (`table_name`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=2 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}

void ZdbStorage::createCnsTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_CNS << "` (\n";
    ss << getCommonFileds();
    ss << "`name` varchar(128) DEFAULT NULL,\n";
    ss << "`version` varchar(128) DEFAULT NULL,\n";
    ss << "`address` varchar(256) DEFAULT NULL,\n";
    ss << "`abi` " << m_valueFieldType << ",\n";
    ss << "PRIMARY KEY (`_id_`),\n";
    ss << "KEY `name` (`name`)\n";
    ss << ") " << m_rowFormat << " ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createAccessTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_ACCESS_TABLE << "` (\n";
    ss << getCommonFileds();
    ss << " `table_name` varchar(128) DEFAULT NULL,\n";
    ss << "`address` varchar(128) DEFAULT NULL,\n";
    ss << " `enable_num` varchar(256) DEFAULT NULL,\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << "KEY `table_name` (`table_name`)\n";
    ss << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createCurrentStateTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_CURRENT_STATE << "` (\n";
    ss << getCommonFileds();
    ss << "`key` varchar(128) DEFAULT NULL,\n";
    ss << "`value` longtext,\n";
    ss << "PRIMARY KEY (`_id_`),\n";
    ss << "KEY `key` (`key`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=3 DEFAULT CHARSET=utf8mb4;\n";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createNumber2HashTables()
{
    stringstream ss;

    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_NUMBER_2_HASH << "` (\n";
    ss << getCommonFileds();
    ss << " `number` varchar(128) DEFAULT NULL,\n";
    ss << " `value` longtext,\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << " KEY `number` (`number`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=24 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createTxHash2BlockTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_TX_HASH_2_BLOCK << "` (\n";
    ss << getCommonFileds();
    ss << "`hash` varchar(128) DEFAULT NULL,\n";
    ss << "`value` longtext,\n";
    ss << "`index` varchar(256) DEFAULT NULL,\n";
    ss << "PRIMARY KEY (`_id_`),\n";
    ss << "KEY `hash` (`hash`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=20 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createHash2BlockTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_HASH_2_BLOCK << "` (\n";
    ss << getCommonFileds();
    ss << "`hash` varchar(128) DEFAULT NULL,\n";
    ss << "`value` " << m_valueFieldType << ",\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << "KEY `hash` (`hash`)\n";
    ss << ") " << m_rowFormat << " ENGINE=InnoDB AUTO_INCREMENT=10 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}

void ZdbStorage::createBlobSysHash2BlockHeaderTable()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_HASH_2_BLOCKHEADER << "` (\n";
    ss << getCommonFileds();
    ss << "`hash` varchar(128) DEFAULT NULL,\n";
    ss << "`value` longblob,\n";
    ss << "`sigs` longblob,\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << "KEY `hash` (`hash`)\n";
    ss << ") " << m_rowFormat << " ENGINE=InnoDB AUTO_INCREMENT=10 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}


void ZdbStorage::createSysConsensus()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_CONSENSUS << "` (\n";
    ss << getCommonFileds();
    ss << "`name` varchar(128) DEFAULT 'node',\n";
    ss << "`type` varchar(128) DEFAULT NULL,\n";
    ss << "`node_id` varchar(256) DEFAULT NULL,\n";
    ss << " `enable_num` varchar(256) DEFAULT NULL,\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << "KEY `_num_` (`_num_`),\n";
    ss << "KEY `name` (`name`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=5 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createSysConfigTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_CONFIG << "` (\n";
    ss << getCommonFileds();
    ss << "`key` varchar(128) DEFAULT NULL,\n";
    ss << "`value` longtext,\n";
    ss << "`enable_num` varchar(256) DEFAULT NULL,\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << "KEY `key` (`key`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=9 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createSysBlock2NoncesTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << SYS_BLOCK_2_NONCES << "` (\n";
    ss << getCommonFileds();
    ss << "`number` varchar(128) DEFAULT NULL,\n";
    ss << " `value` " << m_valueFieldType << ",\n";
    ss << "PRIMARY KEY (`_id_`),";
    ss << "KEY `number` (`number`)";
    ss << ") " << m_rowFormat << " ENGINE=InnoDB AUTO_INCREMENT=6 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}

void ZdbStorage::insertSysTables()
{
    stringstream ss;
    ss << "insert ignore into  `" << SYS_TABLES
       << "` ( `table_name` , `key_field`, `value_field`)values \n";
    ss << "	('" << SYS_TABLES << "', 'table_name','key_field,value_field'),\n";
    ss << "	('" << SYS_CONSENSUS << "', 'name','type,node_id,enable_num'),\n";
    ss << "	('" << SYS_ACCESS_TABLE << "', 'table_name','address,enable_num'),\n";
    ss << "	('" << SYS_CURRENT_STATE << "', 'key','value'),\n";
    ss << "	('" << SYS_NUMBER_2_HASH << "', 'number','value'),\n";
    ss << "	('" << SYS_TX_HASH_2_BLOCK << "', 'hash','value,index'),\n";
    ss << "	('" << SYS_HASH_2_BLOCK << "', 'hash','value'),\n";
    ss << "	('" << SYS_CNS << "', 'name','version,address,abi'),\n";
    ss << "	('" << SYS_CONFIG << "', 'key','value,enable_num'),\n";
    ss << "	('" << SYS_BLOCK_2_NONCES << "', 'number','value'),\n";
    ss << "	('" << SYS_HASH_2_BLOCKHEADER << "', 'hash','value,sigs');";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
