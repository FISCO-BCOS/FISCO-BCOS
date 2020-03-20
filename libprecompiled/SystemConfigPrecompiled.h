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
/** @file SystemConfigPrecompiled.h
 *  @author chaychen
 *  @date 20181211
 */
#pragma once
#include "Common.h"
namespace dev
{
namespace precompiled
{
const char* const SYSTEM_CONFIG_KEY = "key";
const char* const SYSTEM_CONFIG_VALUE = "value";
const char* const SYSTEM_CONFIG_ENABLENUM = "enable_num";
const char* const SYSTEM_KEY_TX_COUNT_LIMIT = "tx_count_limit";
const char* const SYSTEM_INIT_VALUE_TX_COUNT_LIMIT = "1000";
const char* const SYSTEM_KEY_TX_GAS_LIMIT = "tx_gas_limit";

// system configuration for RPBFT
const char* const SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM = "rpbft_epoch_sealer_num";
const char* const SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM = "rpbft_epoch_block_num";

const char* const SYSTEM_INIT_VALUE_TX_GAS_LIMIT = "300000000";

const int TX_COUNT_LIMIT_MIN = 1;
const int TX_GAS_LIMIT_MIN = 100000;

const int RPBFT_EPOCH_SEALER_NUM_MIN = 1;
const int RPBFT_EPOCH_BLOCK_NUM_MIN = 1;

#if 0
contract SystemConfigTable
{
    // Return 1 means successful setting, and 0 means cannot find the config key.
    function setValueByKey(string key, string value) public returns(int256);
}
#endif

class SystemConfigPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<SystemConfigPrecompiled> Ptr;
    SystemConfigPrecompiled();
    virtual ~SystemConfigPrecompiled(){};

    bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context, bytesConstRef param,
        Address const& origin = Address(), Address const& _sender = Address()) override;

private:
    bool checkValueValid(std::string const& key, std::string const& value);
};

}  // namespace precompiled

}  // namespace dev
