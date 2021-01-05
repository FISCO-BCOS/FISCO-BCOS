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
 * (c) 2016-2021 fisco-dev contributors.
 */
/** @file AccountInfoUtility.cpp
 *  @author yujiechen
 *  @date 20210105
 */
#include "AccountInfoUtility.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libstorage/Table.h>
#include <libstoragestate/StorageState.h>


using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::storagestate;
using namespace dev::precompiled;

dev::precompiled::ContractStatus dev::precompiled::getContractStatus(
    ExecutiveContext::Ptr context, std::string const& tableName)
{
    Table::Ptr table = dev::precompiled::openTable(context, tableName);
    if (!table)
    {
        return ContractStatus::AddressNonExistent;
    }

    auto codeHashEntries = table->select(ACCOUNT_CODE_HASH, table->newCondition());
    h256 codeHash;
    if (g_BCOSConfig.version() >= V2_5_0)
    {
        codeHash = h256(codeHashEntries->get(0)->getFieldBytes(STORAGE_VALUE));
    }
    else
    {
        codeHash = h256(fromHex(codeHashEntries->get(0)->getField(STORAGE_VALUE)));
    }

    if (EmptyHash == codeHash)
    {
        return ContractStatus::NotContractAddress;
    }

    auto frozenEntries = table->select(ACCOUNT_FROZEN, table->newCondition());
    if (frozenEntries->size() > 0 && "true" == frozenEntries->get(0)->getField(STORAGE_VALUE))
    {
        return ContractStatus::Frozen;
    }
    else
    {
        return ContractStatus::Available;
    }
    PRECOMPILED_LOG(ERROR) << LOG_DESC("getContractStatus error")
                           << LOG_KV("table name", tableName);

    return ContractStatus::Invalid;
}

std::pair<dev::precompiled::AccountStatus, std::shared_ptr<dev::storage::Table>>
dev::precompiled::getAccountStatus(ExecutiveContext::Ptr _context, std::string const& _tableName)
{
    Table::Ptr table = dev::precompiled::openTable(_context, _tableName);
    std::pair<AccountStatus, std::shared_ptr<dev::storage::Table>> accountStatusRet;
    accountStatusRet.second = table;
    if (!table)
    {
        accountStatusRet.first = AccountStatus::AccAddressNonExistent;
        return accountStatusRet;
    }

    auto codeHashEntries = table->select(ACCOUNT_CODE_HASH, table->newCondition());
    if (EmptyHash != h256(codeHashEntries->get(0)->getFieldBytes(STORAGE_VALUE)))
    {
        accountStatusRet.first = AccountStatus::InvalidAccountAddress;
        return accountStatusRet;
    }

    auto frozenEntries = table->select(ACCOUNT_FROZEN, table->newCondition());
    if (frozenEntries->size() > 0 && "true" == frozenEntries->get(0)->getField(STORAGE_VALUE))
    {
        accountStatusRet.first = AccountStatus::AccFrozen;
        return accountStatusRet;
    }
    else
    {
        accountStatusRet.first = AccountStatus::AccAvailable;
        return accountStatusRet;
    }
    PRECOMPILED_LOG(ERROR) << LOG_DESC("getAccountStatus error")
                           << LOG_KV("account table name", _tableName);

    accountStatusRet.first = AccountStatus::AccInvalid;
    return accountStatusRet;
}

int dev::precompiled::parseAccountStatus(AccountStatus const& _status)
{
    int retCode;
    switch (_status)
    {
    case AccountStatus::AccAvailable:
        retCode = CODE_SUCCESS;
        break;
    case AccountStatus::AccInvalid:
        retCode = CODE_INVALID_ACCOUNT_ADDRESS;
        break;
    case AccountStatus::AccFrozen:
        retCode = CODE_ACCOUNT_FROZEN;
        break;
    case AccountStatus::AccAddressNonExistent:
        retCode = CODE_ACCOUNT_NOT_EXIST;
        break;
    case AccountStatus::InvalidAccountAddress:
        retCode = CODE_INVALID_ACCOUNT_ADDRESS;
        break;
    default:
        retCode = CODE_INVALID_ACCOUNT_ADDRESS;
        break;
    }
    return retCode;
}

bool dev::precompiled::isCommitteeMember(ExecutiveContext::Ptr _context, Address const& _account)
{
    auto acTable = dev::precompiled::openTable(_context, SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, _account.hexPrefixed());
    auto entries = acTable->select(SYS_ACCESS_TABLE, condition);
    if (entries->size() != 0u)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("account is a committee meember")
                               << LOG_KV("account", _account.hexPrefixed());
        return true;
    }
    return false;
}