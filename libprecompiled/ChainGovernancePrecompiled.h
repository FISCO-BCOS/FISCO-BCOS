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
/** @file ChainGovernancePrecompiled.h
 *  @author xingqiangbai
 *  @date 20190324
 */
#pragma once
#include "Common.h"

namespace dev
{
namespace storage
{
class Table;
}

namespace precompiled
{
enum AccountStatus
{
    AccInvalid = 0,
    AccAvailable,
    AccFrozen,
    AccAddressNonExistent,
    InvalidAccountAddress,
    AccCount
};

class ChainGovernancePrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<ChainGovernancePrecompiled> Ptr;
    ChainGovernancePrecompiled();
    virtual ~ChainGovernancePrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;

protected:
    void addPrefixToUserTable(std::string& _tableName);

    enum Operation : int
    {
        GrantCommitteeMember,
        RevokeCommitteeMember,
        UpdateCommitteeMemberWeight,
        UpdateThreshold,
    };

private:
    std::shared_ptr<dev::storage::Table> getCommitteeTable(
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context);
    int verifyAndRecord(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        Operation _op, const std::string& _user, const std::string& _value,
        const std::string& _origin);
    std::string queryCommitteeMembers(
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        const std::string& _tableName);
    int grantCommitteeMember(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        const std::string& _user, const Address& _origin);
    int updateCommitteeMemberWeight(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        const std::string& _member, const std::string& _weight, const Address& _origin);
    int grantOperator(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        const std::string& _userAddress, const Address& _origin);
    int revokeOperator(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        const std::string& _userAddress, const Address& _origin);

    int grantTablePermission(std::shared_ptr<blockverifier::ExecutiveContext> _context,
        const std::string& _tableName, const std::string& _userAddress, const Address& _origin);
    bool checkPermission(
        std::shared_ptr<blockverifier::ExecutiveContext> context, Address const& origin);
    AccountStatus getAccountStatus(
        std::shared_ptr<blockverifier::ExecutiveContext> context, std::string const& tableName);
    int updateFrozenStatus(std::shared_ptr<blockverifier::ExecutiveContext> context,
        std::string const& tableName, std::string const& frozen, Address const& origin);
    void freezeAccount(std::shared_ptr<blockverifier::ExecutiveContext> context, bytesConstRef data,
        Address const& origin, PrecompiledExecResult::Ptr _callResult);
    void unfreezeAccount(std::shared_ptr<blockverifier::ExecutiveContext> context,
        bytesConstRef data, Address const& origin, PrecompiledExecResult::Ptr _callResult);
    void getAccountStatus(std::shared_ptr<blockverifier::ExecutiveContext> context,
        bytesConstRef data, PrecompiledExecResult::Ptr _callResult);
};

}  // namespace precompiled

}  // namespace dev
