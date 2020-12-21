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
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Implementation of working sealer management function
 * @file: WorkingSealerManagerPrecompiled.h
 * @author: yujiechen
 * @date: 2020-06-04
 */

#pragma once
#include "Precompiled.h"
#include <libprotocol/ContractABICodec.h>

namespace bcos
{
namespace precompiled
{
#if 0
// Note: strongly discouraged that users use this interface
contract WorkingSealerManagerPrecompiled
{
    function rotateWorkingSealer(string, string, string, ssize_t, ssize_t) returns(int);
}
#endif
// The parameters are: VRF proof, VRF public key, VRF input
// In order to support the vrfBasedrPBFT to generate rotateWorkingSealer transactions,
// the interface name is exposed in the header file
extern const char* const WSM_METHOD_ROTATE_STR;

class WorkingSealerManagerPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<WorkingSealerManagerPrecompiled>;
    WorkingSealerManagerPrecompiled();
    virtual ~WorkingSealerManagerPrecompiled() {}

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;

private:
    void rotateWorkingSealer(std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context,
        bytesConstRef _paramData, bcos::protocol::ContractABICodec& _abi, Address const& _origin,
        Address const& _sender);
};
}  // namespace precompiled
}  // namespace bcos
