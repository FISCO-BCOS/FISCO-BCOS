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
/** @file ContractLifeCyclePrecompiled.h
 *  @author chaychen
 *  @date 20190106
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

const std::string CONTRACT_STATUS_DESC[ContractStatus::Count] = {"Invalid",
    "The contract is available.",
    "The contract has been frozen. You can invoke this contract after unfreezing it.",
    "The address is nonexistent.", "This is not a contract address."};

const std::string STATUS_TRUE = "true";
const std::string STATUS_FALSE = "false";

class ContractLifeCyclePrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<ContractLifeCyclePrecompiled> Ptr;
    ContractLifeCyclePrecompiled();
    virtual ~ContractLifeCyclePrecompiled(){};

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address(),
        Address const& _sender = Address()) override;

private:
    bool checkPermission(std::shared_ptr<blockverifier::ExecutiveContext> context,
        std::string const& tableName, Address const& origin);
    ContractStatus getContractStatus(
        std::shared_ptr<blockverifier::ExecutiveContext> context, std::string const& tableName);
    int updateFrozenStatus(std::shared_ptr<blockverifier::ExecutiveContext> context,
        std::string const& tableName, std::string const& frozen, Address const& origin);
    void freeze(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, PrecompiledExecResult::Ptr _callResult);
    void unfreeze(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, PrecompiledExecResult::Ptr _callResult);
    void grantManager(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, PrecompiledExecResult::Ptr _callResult);

    void revokeManager(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, PrecompiledExecResult::Ptr _callResult);

    bool checkContractManager(std::string const& _tableName,
        std::shared_ptr<blockverifier::ExecutiveContext> _context, Address const& _contractAddress,
        Address const& _userAddress, Address const& _origin, int& _result);

    void getStatus(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        PrecompiledExecResult::Ptr _callResult);
    void listManager(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        PrecompiledExecResult::Ptr _callResult);
};

}  // namespace precompiled

}  // namespace dev
