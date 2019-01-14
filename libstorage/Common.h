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
#include <json_spirit/JsonSpiritHeaders.h>
#include <string>

namespace dev
{
namespace storage
{
#define STORAGE_LOG(LEVEL) LOG(LEVEL) << "[STORAGE]"
#define STORAGE_LEVELDB_LOG(LEVEL) LOG(LEVEL) << "[STORAGE] [LEVELDB]"

/// \brief Sign of the DB key is valid or not
const std::string STATUS = "_status_";
const std::string SYS_TABLES = "_sys_tables_";
const std::string SYS_MINERS = "_sys_miners_";
const std::string SYS_CURRENT_STATE = "_sys_current_state_";
const std::string SYS_KEY_CURRENT_NUMBER = "current_number";
const std::string SYS_KEY_TOTAL_TRANSACTION_COUNT = "total_current_transaction_count";
const std::string SYS_VALUE = "value";
const std::string SYS_KEY = "key";
const std::string SYS_TX_HASH_2_BLOCK = "_sys_tx_hash_2_block_";
const std::string SYS_NUMBER_2_HASH = "_sys_number_2_hash_";
const std::string SYS_HASH_2_BLOCK = "_sys_hash_2_block_";
const std::string SYS_CNS = "_sys_cns_";
const std::string SYS_CONFIG = "_sys_config_";
const std::string SYS_ACCESS_TABLE = "_sys_table_access_";
const std::string USER_TABLE_PREFIX = "_user_";
const std::string SYS_BLOCK_2_NONCES = "_sys_block_2_nonces_";
const std::string DAG_TRANSFER = "_dag_transfer_";

}  // namespace storage
}  // namespace dev
