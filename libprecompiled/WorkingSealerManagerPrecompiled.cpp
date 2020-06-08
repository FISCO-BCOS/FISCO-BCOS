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
 * @file: WorkingSealerManagerPrecompiled.cpp
 * @author: yujiechen
 * @date: 2020-06-04
 */
#include "WorkingSealerManagerPrecompiled.h"

using namespace dev::precompiled;

// The parameters are: VRF public key, VRF input, VRF proof, removedWorkingSealers,
// insertedWorkingSealers
const char* const dev::precompiled::WSM_METHOD_ROTATE_STR =
    "rotateWorkingSealer(std::string, std::string, std::string, uint256, uint256)";

// init function selector
WorkingSealerManagerPrecompiled::WorkingSealerManagerPrecompiled()
{
    name2Selector[WSM_METHOD_ROTATE_STR] = getFuncSelector(WSM_METHOD_ROTATE_STR);
}

std::string WorkingSealerManagerPrecompiled::toString()
{
    return "WorkingSealerManagerPrecompiled";
}


void WorkingSealerManagerPrecompiled::rotateWorkingSealer(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _paramData,
    dev::eth::ContractABI& _abi, PrecompiledExecResult::Ptr _result)
{
    std::string vrfProof;
    std::string vrfPublicKey;
    std::string vrfInput;
    u256 insertedNodeNum;
    u256 removedNodeNum;
    _abi.abiOut(_paramData, vrfProof, vrfPublicKey, vrfInput, insertedNodeNum, removedNodeNum);

    // TODO: implement rotateWorkingSealer
    (void)_context;
    (void)_result;
}

PrecompiledExecResult::Ptr WorkingSealerManagerPrecompiled::call(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _param,
    Address const&, Address const&)
{
    // get function selector
    auto funcSelector = getParamFunc(_param);
    auto paramData = getParamData(_param);
    dev::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    // Call the corresponding function according to the selector
    if (funcSelector == name2Selector[WSM_METHOD_ROTATE_STR])
    {
        rotateWorkingSealer(_context, paramData, abi, callResult);
    }
    return callResult;
}