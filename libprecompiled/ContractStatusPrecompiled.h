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
/** @file ContractStatusPrecompiled.h
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
    Killed,
    Nonexistent,
    Count
};

const std::string CONTRACT_STATUS_DESC[ContractStatus::Count] = {"Invalid",
    "The contract is available.",
    "The contract has been frozen. You can invoke this contract after unfrozening it.",
    "The contract has been killed. You can no longer invoke this contract.",
    "The contract is nonexistent."};

const std::string STATUS_TRUE = "true";
const std::string STATUS_FALSE = "false";

class ContractStatusPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<ContractStatusPrecompiled> Ptr;
    ContractStatusPrecompiled();
    virtual ~ContractStatusPrecompiled(){};

    virtual bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address());

private:
    bool checkAddress(Address const& contractAddress);
    bool checkPermission(std::shared_ptr<blockverifier::ExecutiveContext> context,
        std::string const& tableName, Address const& origin);
    ContractStatus getContractStatus(
        std::shared_ptr<blockverifier::ExecutiveContext> context, std::string const& tableName);
    void kill(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, bytes& out);
    int updateFrozenStatus(std::shared_ptr<blockverifier::ExecutiveContext> context,
        std::string const& tableName, std::string const& frozen, Address const& origin);
    void freeze(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, bytes& out);
    void unfreeze(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, bytes& out);
    void queryStatus(
        std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data, bytes& out);
    void queryAuthority(
        std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data, bytes& out);
};

}  // namespace precompiled

}  // namespace dev
