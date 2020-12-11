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
/** @file Common.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once
#include <map>
#include <string>

namespace dev
{
namespace storage
{
#define STORAGE_LOG(LEVEL) LOG(LEVEL) << "[STORAGE]"
#define STORAGE_LEVELDB_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("STORAGE") << LOG_BADGE("LevelDB")
#define STORAGE_ROCKSDB_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("STORAGE") << LOG_BADGE("RocksDB")
#define STORAGE_EXTERNAL_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("STORAGE") << LOG_BADGE("External")
#define CACHED_STORAGE_LOG(LEVEL)                                                   \
    LOG(LEVEL) << "[g:" << std::to_string(groupID()) << "]" << LOG_BADGE("STORAGE") \
               << LOG_BADGE("CachedStorage")
#define BINLOG_HANDLER_LOG(LEVEL) LOG(LEVEL) << "[BINLOG]"
#define SCALABLE_STORAGE_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("ScalableStorage")

static const std::string ID_FIELD = "_id_";
static const std::string NUM_FIELD = "_num_";
static const std::string STATUS = "_status_";
static const std::string SYS_KEY_CURRENT_NUMBER = "current_number";
static const std::string SYS_KEY_CURRENT_ID = "current_id";
static const std::string SYS_KEY_TOTAL_TRANSACTION_COUNT = "total_transaction_count";
static const std::string SYS_KEY_TOTAL_FAILED_TRANSACTION = "total_failed_transaction_count";
static const std::string SYS_VALUE = "value";
static const std::string SYS_SIG_LIST = "sigs";
static const std::string SYS_KEY = "key";
static const std::string SYS_ASSET_NAME = "name";
static const std::string SYS_ASSET_FUNGIBLE = "fungible";
static const std::string SYS_ASSET_TOTAL = "total";
static const std::string SYS_ASSET_SUPPLIED = "supplied";
static const std::string SYS_ASSET_ISSUER = "issuer";
static const std::string SYS_ASSET_DESCRIPTION = "description";

static const std::string SYS_TABLES = "_sys_tables_";
static const std::string SYS_CONSENSUS = "_sys_consensus_";
static const std::string SYS_CURRENT_STATE = "_sys_current_state_";
static const std::string SYS_TX_HASH_2_BLOCK = "_sys_tx_hash_2_block_";
static const std::string SYS_NUMBER_2_HASH = "_sys_number_2_hash_";
static const std::string SYS_HASH_2_BLOCK = "_sys_hash_2_block_";
static const std::string SYS_CNS = "_sys_cns_";
static const std::string SYS_CONFIG = "_sys_config_";
static const std::string SYS_ACCESS_TABLE = "_sys_table_access_";
static const std::string SYS_BLOCK_2_NONCES = "_sys_block_2_nonces_";
static const std::string SYS_HASH_2_BLOCKHEADER = "_sys_hash_2_header_";
static const std::string SYS_ASSET_INFO = "_sys_asset_info_";


#if 0
const char* const ID_FIELD = "_id_";
const char* const NUM_FIELD = "_num_";
const char* const STATUS = "_status_";
const char* const SYS_TABLES = "_sys_tables_";
const char* const SYS_CONSENSUS = "_sys_consensus_";
const char* const SYS_CURRENT_STATE = "_sys_current_state_";
const char* const SYS_KEY_CURRENT_NUMBER = "current_number";
const char* const SYS_KEY_CURRENT_ID = "current_id";
const char* const SYS_KEY_TOTAL_TRANSACTION_COUNT = "total_transaction_count";
const char* const SYS_VALUE = "value";
const char* const SYS_KEY = "key";
const char* const SYS_TX_HASH_2_BLOCK = "_sys_tx_hash_2_block_";
const char* const SYS_NUMBER_2_HASH = "_sys_number_2_hash_";
const char* const SYS_HASH_2_BLOCK = "_sys_hash_2_block_";
const char* const SYS_CNS = "_sys_cns_";
const char* const SYS_CONFIG = "_sys_config_";
const char* const SYS_ACCESS_TABLE = "_sys_table_access_";
const char* const SYS_BLOCK_2_NONCES = "_sys_block_2_nonces_";
#endif

const int CODE_NO_AUTHORIZED = -50000;
const int CODE_TABLE_NAME_ALREADY_EXIST = -50001;
const int CODE_TABLE_NAME_LENGTH_OVERFLOW = -50002;
const int CODE_TABLE_FIELD_LENGTH_OVERFLOW = -50003;
const int CODE_TABLE_FIELD_TOTALLENGTH_OVERFLOW = -50004;
const int CODE_TABLE_KEYVALUE_LENGTH_OVERFLOW = -50005;
const int CODE_TABLE_FIELDVALUE_LENGTH_OVERFLOW = -50006;
const int CODE_TABLE_DUPLICATE_FIELD = -50007;
const int CODE_TABLE_INVALIDATE_FIELD = -50008;

enum SQLFieldType : int8_t
{
    MediumBlobType = 0,
    LongBlobType = 1,
    MediumStringType = 2,
    LongStringType = 3,
};
extern std::map<SQLFieldType, std::string> SQLFieldTypeName;

bool isHashField(const std::string& _key);
}  // namespace storage
}  // namespace dev
