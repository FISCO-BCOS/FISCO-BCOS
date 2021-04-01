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
 * (c) 2016-2019 fisco-dev contributors.
 *
 *  @file CryptoPrecompiled.h
 *  @author yujiechen
 *  @date 20201202
 */
#pragma once
#include "libprecompiled/Common.h"
#include "libprecompiled/Precompiled.h"

namespace dev
{
namespace precompiled
{
#if 0
contract Crypto
{
    function sm3(bytes data) public view returns(bytes32);
    function keccak256Hash(bytes data) public view returns(bytes32);
    function sm2Verify(bytes32 message, bytes publicKey, bytes32 r, bytes32 s) public view returns(bool, address);
    function curve25519VRFVerify(string input, string vrfPublicKey, string vrfProof) public view returns(bool,uint256);
}
#endif
class CryptoPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<CryptoPrecompiled>;
    CryptoPrecompiled();
    virtual ~CryptoPrecompiled() {}
    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;

private:
    void sm2Verify(bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult);
    void curve25519VRFVerify(bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult);
};
}  // namespace precompiled
}  // namespace dev