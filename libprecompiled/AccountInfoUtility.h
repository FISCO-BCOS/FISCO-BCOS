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
/** @file AccountInfoUtility.h
 *  @author yujiechen
 *  @date 20210105
 */
#pragma once
#include "Common.h"

namespace dev
{
namespace precompiled
{
enum ContractStatus
{
    Invalid = 0,
    Available,
    Frozen,
    AddressNonExistent,
    NotContractAddress,
    Count
};

enum AccountStatus
{
    AccInvalid = 0,
    AccAvailable,
    AccFrozen,
    AccAddressNonExistent,
    InvalidAccountAddress,
    AccCount
};
dev::precompiled::ContractStatus getContractStatus(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> context, std::string const& tableName);

std::pair<dev::precompiled::AccountStatus, std::shared_ptr<dev::storage::Table>> getAccountStatus(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, std::string const& _tableName);

int parseAccountStatus(dev::precompiled::AccountStatus const& _status);
bool isCommitteeMember(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, Address const& _account);
}  // namespace precompiled
}  // namespace dev