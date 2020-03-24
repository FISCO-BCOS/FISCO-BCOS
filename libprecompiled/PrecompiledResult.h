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
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file PrecompiledResult.h
 *  @author yujiechen
 *  @date 20200324
 */
#pragma once
#include "PrecompiledGas.h"
#include <libdevcore/Common.h>

namespace dev
{
namespace precompiled
{
class PrecompiledExecResult
{
public:
    using Ptr = std::shared_ptr<PrecompiledExecResult>;
    PrecompiledExecResult() = default;
    virtual ~PrecompiledExecResult() {}
    bytes const& execResult() const { return m_execResult; }

    void setExecResult(bytes const& _execResult) { m_execResult = _execResult; }

    PrecompiledGas::Ptr gasPricer() { return m_gasPricer; }
    void setGasPricer(PrecompiledGas::Ptr _gasPricer) { m_gasPricer = _gasPricer; }

private:
    bytes m_execResult;
    PrecompiledGas::Ptr m_gasPricer;
};

class PrecompiledExecResultFactory
{
public:
    using Ptr = std::shared_ptr<PrecompiledExecResultFactory>;
    PrecompiledExecResultFactory() = default;
    virtual ~PrecompiledExecResultFactory() {}
    void setPrecompiledGasFactory(PrecompiledGasFactory::Ptr _precompiledGasFactory)
    {
        m_precompiledGasFactory = _precompiledGasFactory;
    }
    PrecompiledExecResult::Ptr createPrecompiledResult()
    {
        auto result = std::make_shared<PrecompiledExecResult>();
        result->setGasPricer(m_precompiledGasFactory->createPrecompiledGas());
        return result;
    }

private:
    PrecompiledGasFactory::Ptr m_precompiledGasFactory;
};
}  // namespace precompiled
}  // namespace dev
