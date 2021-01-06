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
/**
 * @brief : Implementation of GasChargeManagePrecompiled
 * @file: GasChargeManagePrecompiled.h
 * @author: yujiechen
 * @date: 2021-01-05
 */

#pragma once
#include "AccountInfoUtility.h"
#include "Precompiled.h"

#if 0
contract GasChargeManagePrecompiled
{
    function charge(address userAccount, uint256 gasValue) public returns(int256, uint256){}
    function deduct(address userAccount, uint256 gasValue) public returns(int256, uint256){}
    function queryRemainGas(address userAccount) public view returns(uint256){}

    function grantCharger(address chargerAccount) public returns(int256){}
    function revokeCharger(address chargerAccount) public returns(int256){}
    function listChargers() public view returns(address[]){}
}
#endif

namespace dev
{
namespace precompiled
{
class GasChargeManagePrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<GasChargeManagePrecompiled>;
    GasChargeManagePrecompiled();
    virtual ~GasChargeManagePrecompiled() {}

    std::string toString() override { return "GasChargeManagePrecompiled"; }

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;

private:
    void charge(PrecompiledExecResult::Ptr _callResult,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _paramData,
        Address const& _origin, Address const& _sender);

    void deduct(PrecompiledExecResult::Ptr _callResult,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _paramData,
        Address const& _origin, Address const& _sender);

    void queryRemainGas(PrecompiledExecResult::Ptr _callResult,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _paramData,
        Address const& _origin);

    bool checkAccountStatus(PrecompiledExecResult::Ptr _callResult,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        Address const& _userAccount);

    bool checkParams(PrecompiledExecResult::Ptr _callResult,
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, Address const& _userAccount,
        u256 const& _gasValue, Address const& _origin, Address const& _sender);

    std::shared_ptr<std::set<Address>> getChargerList(
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context);
    std::shared_ptr<std::set<Address>> parseChargerList(std::string const& _chargerListStr);

private:
    std::string const c_chargerListSplitStr = ",";
};
}  // namespace precompiled
}  // namespace dev