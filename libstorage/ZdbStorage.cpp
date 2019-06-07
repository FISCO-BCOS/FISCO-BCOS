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
#include <libdevcore/easylog.h>


using namespace std;
using namespace dev;
using namespace dev::storage;

ZdbStorage::ZdbStorage() {}

Entries::Ptr ZdbStorage::select(h256 _hash, int _num, TableInfo::Ptr _tableInfo,
    const std::string& _key, Condition::Ptr _condition)
{
    Json::Value responseJson;
    int ret = m_sqlBasicAcc->Select(_hash, _num, _tableInfo->name, _key, _condition, responseJson);
    if (ret < 0)
    {
        ZdbStorage_LOG(ERROR) << "Remote select datdbase return error:" << ret
                              << " table:" << _tableInfo->name;
        auto e =
            StorageException(-1, "Remote select database return error: table:" + _tableInfo->name +
                                     boost::lexical_cast<std::string>(ret));
        m_fatalHandler(e);
        BOOST_THROW_EXCEPTION(e);
    }

    ZdbStorage_LOG(DEBUG) << " tablename:" << _tableInfo->name
                          << "select resp:" << responseJson.toStyledString();
    std::vector<std::string> columns;
    for (Json::ArrayIndex i = 0; i < responseJson["result"]["columns"].size(); ++i)
    {
        columns.push_back(responseJson["result"]["columns"].get(i, "").asString());
    }

    Entries::Ptr entries = std::make_shared<Entries>();
    for (Json::ArrayIndex i = 0; i < responseJson["result"]["data"].size(); ++i)
    {
        Json::Value line = responseJson["result"]["data"].get(i, "");
        Entry::Ptr entry = std::make_shared<Entry>();

        for (Json::ArrayIndex j = 0; j < line.size(); ++j)
        {
            if (columns[j] == ID_FIELD)
            {
                entry->setID(line.get(j, "").asString());
            }
            else if (columns[j] == NUM_FIELD)
            {
                entry->setNum(line.get(j, "").asString());
            }
            else if (columns[j] == STATUS)
            {
                entry->setStatus(line.get(j, "").asString());
            }
            else
            {
                std::string fieldValue = line.get(j, "").asString();
                entry->setField(columns[j], fieldValue);
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

void ZdbStorage::setConnPool(SQLConnectionPool::Ptr& _connPool)
{
    m_sqlBasicAcc->setConnPool(_connPool);
    this->initSysTables();
}

void ZdbStorage::SetSqlAccess(SQLBasicAccess::Ptr _sqlBasicAcc)
{
    m_sqlBasicAcc = _sqlBasicAcc;
}

size_t ZdbStorage::commit(h256 _hash, int64_t _num, const std::vector<TableData::Ptr>& _datas)
{
    int32_t rowCount = m_sqlBasicAcc->Commit(_hash, (int32_t)_num, _datas);
    if (rowCount < 0)
    {
        ZdbStorage_LOG(ERROR) << "database commit  return error:" << rowCount;
        auto e = StorageException(-1, "Remote select database return error: table:" +
                                          boost::lexical_cast<std::string>(rowCount));
        m_fatalHandler(e);
        BOOST_THROW_EXCEPTION(e);
    }
    return rowCount;
}

bool ZdbStorage::onlyDirty()
{
    return true;
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
    createHash2BlockTables();
    createCnsTables();
    createSysConfigTables();
    createSysBlock2NoncesTables();
    insertSysTables();
}
void ZdbStorage::createSysTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `_sys_tables_` (\n";
    ss << " `_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT '',\n";
    ss << " `_num_` int(11) DEFAULT '0',\n";
    ss << " `_status_` int(11) DEFAULT '0',\n";
    ss << "`table_name` varchar(128) DEFAULT '',\n";
    ss << "`key_field` varchar(1024) DEFAULT '',\n";
    ss << " `value_field` varchar(1024) DEFAULT '',\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << " UNIQUE KEY `table_name` (`table_name`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=62 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createSysConsensus()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `_sys_cns_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << "`_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
    ss << "`name` varchar(128) DEFAULT NULL,\n";
    ss << "`version` varchar(128) DEFAULT NULL,\n";
    ss << "`address` varchar(256) DEFAULT NULL,\n";
    ss << "`abi` longtext,\n";
    ss << "PRIMARY KEY (`_id_`),\n";
    ss << "KEY `name` (`name`)\n";
    ss << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createAccessTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `_sys_table_access_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << "`_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
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
    ss << "CREATE TABLE IF NOT EXISTS `_sys_current_state_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << "`_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
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

    ss << "CREATE TABLE IF NOT EXISTS `_sys_number_2_hash_` (\n";
    ss << " `_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << " `_hash_` varchar(128) DEFAULT NULL,\n";
    ss << " `_num_` int(11) DEFAULT NULL,\n";
    ss << " `_status_` int(11) DEFAULT NULL,\n";
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
    ss << "CREATE TABLE IF NOT EXISTS `_sys_tx_hash_2_block_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << "`_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
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
    ss << "CREATE TABLE IF NOT EXISTS `_sys_hash_2_block_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << " `_num_` int(11) DEFAULT NULL,\n";
    ss << " `_status_` int(11) DEFAULT NULL,\n";
    ss << "`hash` varchar(128) DEFAULT NULL,\n";
    ss << "`value` longtext,\n";
    ss << " PRIMARY KEY (`_id_`),\n";
    ss << "KEY `hash` (`hash`)\n";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=10 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::createCnsTables()
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `_sys_consensus_` (\n";
    ss << " `_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << " `_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
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
    ss << "CREATE TABLE IF NOT EXISTS `_sys_config_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << "`_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
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
    ss << "CREATE TABLE IF NOT EXISTS `_sys_block_2_nonces_` (\n";
    ss << "`_id_` int(10) unsigned NOT NULL AUTO_INCREMENT,\n";
    ss << "`_hash_` varchar(128) DEFAULT NULL,\n";
    ss << "`_num_` int(11) DEFAULT NULL,\n";
    ss << "`_status_` int(11) DEFAULT NULL,\n";
    ss << "`number` varchar(128) DEFAULT NULL,\n";
    ss << " `value` longtext,\n";
    ss << "PRIMARY KEY (`_id_`),";
    ss << "KEY `number` (`number`)";
    ss << ") ENGINE=InnoDB AUTO_INCREMENT=6 DEFAULT CHARSET=utf8mb4;";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
void ZdbStorage::insertSysTables()
{
    stringstream ss;
    ss << "insert ignore into  `_sys_tables_` ( `table_name` , `key_field`, "
          "`value_field`)values "
          "\n";
    ss << "	('_sys_tables_', 'table_name','key_field,value_field'),\n";
    ss << "	('_sys_consensus_', 'name','type,node_id,enable_num'),\n";
    ss << "	('_sys_table_access_', 'table_name','address,enable_num'),\n";
    ss << "	('_sys_current_state_', 'key','value'),\n";
    ss << "	('_sys_number_2_hash_', 'number','value'),\n";
    ss << "	('_sys_tx_hash_2_block_', 'hash','value,index'),\n";
    ss << "	('_sys_hash_2_block_', 'hash','value'),\n";
    ss << "	('_sys_cns_', 'name','version,address,abi'),\n";
    ss << "	('_sys_config_', 'key','value,enable_num'),\n";
    ss << "	('_sys_block_2_nonces_', 'number','value');";
    string sql = ss.str();
    m_sqlBasicAcc->ExecuteSql(sql);
}
