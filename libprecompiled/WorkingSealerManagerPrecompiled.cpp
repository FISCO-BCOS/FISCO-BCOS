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
#include "WorkingSealerManagerImpl.h"
#include <libblockverifier/ExecutiveContext.h>

using namespace bcos::precompiled;
using namespace bcos::blockverifier;

// The parameters are: VRF public key, VRF input, VRF proof, removedWorkingSealers,
// insertedWorkingSealers
const char* const bcos::precompiled::WSM_METHOD_ROTATE_STR =
    "rotateWorkingSealer(std::string, std::string, std::string)";

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
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context, bytesConstRef _paramData,
    bcos::eth::ContractABI& _abi, Address const& _origin, Address const& _sender)
{
    try
    {
        PRECOMPILED_LOG(INFO) << LOG_DESC("rotateWorkingSealer")
                              << LOG_KV("number", _context->blockInfo().number);
        std::string vrfProof;
        std::string vrfPublicKey;
        std::string vrfInput;
        _abi.abiOut(_paramData, vrfPublicKey, vrfInput, vrfProof);

        auto workingSealerMgr =
            std::make_shared<WorkingSealerManagerImpl>(_context, _origin, _sender);

        workingSealerMgr->createVRFInfo(vrfProof, vrfPublicKey, vrfInput);
        workingSealerMgr->rotateWorkingSealer();
    }
    catch (std::exception const& _e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("WorkingSealerManagerPrecompiled")
                               << LOG_DESC("rotateWorkingSealer exceptioned")
                               << LOG_KV("errorInfo", boost::diagnostic_information(_e))
                               << LOG_KV("origin", _origin.hexPrefixed())
                               << LOG_KV("sender", _sender.hexPrefixed());
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "rotateWorkingSealer exceptioned, error info:" + boost::diagnostic_information(_e) +
            " , origin:" + _origin.hexPrefixed()));
    }
}

PrecompiledExecResult::Ptr WorkingSealerManagerPrecompiled::call(
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context, bytesConstRef _param,
    Address const& _origin, Address const& _sender)
{
    // get function selector
    auto funcSelector = getParamFunc(_param);
    auto paramData = getParamData(_param);
    bcos::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    // Call the corresponding function according to the selector
    if (funcSelector == name2Selector[WSM_METHOD_ROTATE_STR])
    {
        rotateWorkingSealer(_context, paramData, abi, _origin, _sender);
    }
    return callResult;
}
