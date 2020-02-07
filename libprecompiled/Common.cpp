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
#include "Common.h"
#include <libconfig/GlobalConfigure.h>
#include <libethcore/ABI.h>
#include <libethcore/Exceptions.h>

using namespace dev;
using namespace std;


static const string USER_TABLE_PREFIX = "_user_";
static const string USER_TABLE_PREFIX_SHORT = "u_";

void dev::precompiled::getErrorCodeOut(bytes& out, int const& result)
{
    dev::eth::ContractABI abi;
    if (result > 0 && result < 128)
    {
        out = abi.abiIn("", u256(result));
        return;
    }
    out = abi.abiIn("", s256(result));
    if (g_BCOSConfig.version() < RC2_VERSION)
    {
        out = abi.abiIn("", -result);
    }
    else if (g_BCOSConfig.version() == RC2_VERSION)
    {
        out = abi.abiIn("", u256(-result));
    }
}

void dev::precompiled::logError(
    const std::string& precompiled_name, const std::string& op, const std::string& msg)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(precompiled_name) << LOG_DESC(op) << ": " << LOG_DESC(msg);
}

void dev::precompiled::logError(const std::string& precompiled_name, const std::string& op,
    const std::string& _key, const std::string& _value)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(precompiled_name) << LOG_DESC(op) << LOG_KV(_key, _value);
}

void dev::precompiled::logError(const std::string& precompiled_name, const std::string& op,
    const std::string& key1, const std::string& value1, const std::string& key2,
    const std::string& value2)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(precompiled_name) << LOG_DESC(op) << LOG_KV(key1, value1)
                           << LOG_KV(key2, value2);
}

void dev::precompiled::throwException(const std::string& msg)
{
    BOOST_THROW_EXCEPTION(dev::eth::TransactionRefused() << dev::errinfo_comment(msg));
}

char* dev::precompiled::stringToChar(std::string& params)
{
    return const_cast<char*>(params.data());
}

std::string dev::precompiled::getTableName(const std::string& _tableName)
{
    if (g_BCOSConfig.version() < V2_2_0)
    {
        return USER_TABLE_PREFIX + _tableName;
    }
    return USER_TABLE_PREFIX_SHORT + _tableName;
}