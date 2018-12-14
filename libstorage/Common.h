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
/** @file Common.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once
#include <string>

namespace dev
{
namespace storage
{
#define STORAGE_LOG(LEVEL) LOG(LEVEL) << "[#STORAGE] "

/// \brief Sign of the DB key is valid or not
const char* const STATUS = "_status_";
const char* const SYS_TABLES = "_sys_tables_";
const char* const SYS_MINERS = "_sys_miners_";
const std::string SYS_CURRENT_STATE = "_sys_current_state_";
const std::string SYS_KEY_CURRENT_NUMBER = "current_number";
const std::string SYS_KEY_TOTAL_TRANSACTION_COUNT = "total_current_transaction_count";
const std::string SYS_VALUE = "value";
const std::string SYS_TX_HASH_2_BLOCK = "_sys_tx_hash_2_block_";
const std::string SYS_NUMBER_2_HASH = "_sys_number_2_hash_";
const std::string SYS_HASH_2_BLOCK = "_sys_hash_2_block_";

#if FISCO_GM
// ConditionPrecompied
static uint32_t const c_EQ_string_int256 = 0xd62b54b4;
static uint32_t const c_EQ_string_string = 0xae763db5;
static uint32_t const c_GE_string_int256 = 0x40b5f1ab;
static uint32_t const c_GT_string_int256 = 0xe1f10ee0;
static uint32_t const c_LE_string_int256 = 0x2ec346c9;
static uint32_t const c_LT_string_int256 = 0x1ee88791;
static uint32_t const c_NE_string_int256 = 0xf955264b;
static uint32_t const c_NE_string_string = 0x966b0822;
static uint32_t const c_limit_int256 = 0xbd8cb043;
static uint32_t const c_limit_int256_int256 = 0x32492737;
// CRUDPrecompied
static uint32_t const c_select_string_string = 0x4c5eaa97;
// EntriesPrecompied
static uint32_t const c_get_int256 = 0x846719e0;
static uint32_t const c_size = 0xd3e9af5a;
// EntryPrecompied
static uint32_t const c_getInt_string = 0x4900862e;
static uint32_t const c_set_string_int256 = 0xdef42698;
static uint32_t const c_set_string_string = 0x1a391cb4;
static uint32_t const c_getAddress_string = 0x07afbf3a;
static uint32_t const c_getBytes64_string = 0x9139fa37;
// MinerPrecompied
static uint32_t const c_add_string = 0xf3b8900e;
static uint32_t const c_remove_string = 0x86b733f9;
// TableFactoryPrecompied
static uint32_t const c_openDB_string = 0x59c5a99a;
static uint32_t const c_openTable_string = 0x59a48b65;
static uint32_t const c_createTable_string_string_string = 0xc92a7801;
// TablePrecompied
static uint32_t const c_select_string_address = 0xd8ac5957;
static uint32_t const c_insert_string_address = 0x4c6f30c0;
static uint32_t const c_newCondition = 0xc74f8caf;
static uint32_t const c_newEntry = 0x5887ab24;
static uint32_t const c_remove_string_address = 0x09ff42f0;
static uint32_t const c_update_string_address_address = 0x664b37d6;
#else
// ConditionPrecompied
static uint32_t const c_EQ_string_int256 = 0xe44594b9;
static uint32_t const c_EQ_string_string = 0xcd30a1d1;
static uint32_t const c_GE_string_int256 = 0x42f8dd31;
static uint32_t const c_GT_string_int256 = 0x08ad6333;
static uint32_t const c_LE_string_int256 = 0xb6f23857;
static uint32_t const c_LT_string_int256 = 0xc31c9b65;
static uint32_t const c_NE_string_int256 = 0x39aef024;
static uint32_t const c_NE_string_string = 0x2783acf5;
static uint32_t const c_limit_int256 = 0x2e0d738a;
static uint32_t const c_limit_int256_int256 = 0x7ec1cc65;
// CRUDPrecompied
static uint32_t const c_select_string_string = 0x73224cec;
// EntriesPrecompied
static uint32_t const c_get_int256 = 0x846719e0;
static uint32_t const c_size = 0x949d225d;
// EntryPrecompied
static uint32_t const c_getInt_string = 0xfda69fae;
static uint32_t const c_set_string_int256 = 0x2ef8ba74;
static uint32_t const c_set_string_string = 0xe942b516;
static uint32_t const c_getAddress_string = 0xbf40fac1;
static uint32_t const c_getBytes64_string = 0xd52decd4;
// MinerPrecompied
static uint32_t const c_add_string = 0xb0c8f9dc;
static uint32_t const c_remove_string = 0x80599e4b;
// TableFactoryPrecompied
static uint32_t const c_openDB_string = 0xc184e0ff;
static uint32_t const c_openTable_string = 0xf23f63c9;
static uint32_t const c_createTable_string_string_string = 0x56004b6a;
// TablePrecompied
static uint32_t const c_select_string_address = 0xe8434e39;
static uint32_t const c_insert_string_address = 0x31afac36;
static uint32_t const c_newCondition = 0x7857d7c9;
static uint32_t const c_newEntry = 0x13db9346;
static uint32_t const c_remove_string_address = 0x28bb2117;
static uint32_t const c_update_string_address_address = 0xbf2b70a1;
#endif
}  // namespace storage
}  // namespace dev