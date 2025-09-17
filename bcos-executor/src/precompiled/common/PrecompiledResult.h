/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file PrecompiledResult.h
 * @author: kyonRay
 * @date 2021-05-25
 */
#pragma once
#include "../../CallParameters.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-utilities/Common.h>

namespace bcos::precompiled
{
struct PrecompiledExecResult
{
    using Ptr = std::shared_ptr<PrecompiledExecResult>;
    PrecompiledExecResult() = default;
    PrecompiledExecResult(const executor::CallParameters& _callParameters)
      : m_sender(_callParameters.senderAddress),
        m_codeAddress(_callParameters.codeAddress),
        m_precompiledAddress(_callParameters.receiveAddress),
        m_origin(_callParameters.origin),
        m_input(ref(_callParameters.data)),
        m_gasLeft(_callParameters.gas),
        m_staticCall(_callParameters.staticCall),
        m_create(_callParameters.create)
    {}
    ~PrecompiledExecResult() = default;
    PrecompiledExecResult(const PrecompiledExecResult&) = delete;
    PrecompiledExecResult& operator=(const PrecompiledExecResult&) = delete;

    PrecompiledExecResult(PrecompiledExecResult&&) = delete;
    PrecompiledExecResult(const PrecompiledExecResult&&) = delete;

    /** for input **/
    bytesConstRef const& input() const { return m_input; }
    bytesConstRef params() const { return m_input.getCroppedData(4); }

    /** for output **/
    bytes const& execResult() const { return m_execResult; }
    bytes& mutableExecResult() { return m_execResult; }
    void setExecResult(bytes const& _execResult) { m_execResult = _execResult; }
    void setExecResult(bytes&& _execResult) { m_execResult = std::move(_execResult); }
    void setGasLeft(int64_t _gasLeft) { m_gasLeft = _gasLeft; }
    void setExternalResult(executor::CallParameters::UniquePtr _callParameter)
    {
        m_execResult = std::move(_callParameter->data);
        if (_callParameter->status != (int32_t)protocol::TransactionStatus::None)
        {
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError{} << errinfo_comment(
                                      "External call revert: " + _callParameter->message));
        }
    }

    void takeDataToCallParameter(executor::CallParameters::UniquePtr& callParameters)
    {
        callParameters->type = executor::CallParameters::FINISHED;
        callParameters->gas = m_gasLeft;
        callParameters->status = (int32_t)protocol::TransactionStatus::None;
        callParameters->data = std::move(m_execResult);
        callParameters->internalCall = false;
    }

    std::string m_sender;       // common field, readable format
    std::string m_codeAddress;  // different to 'to', this field set to precompiled origin address
    std::string m_precompiledAddress;  // common field, readable format
    std::string m_origin;              // common field, readable format

    bytesConstRef m_input;  // common field, transaction data, binary format
    bcos::bytes m_execResult;
    int64_t m_gasLeft = 0;
    bool m_staticCall = false;  // common field
    bool m_create = false;      // by request, is creation
};
}  // namespace bcos::precompiled
