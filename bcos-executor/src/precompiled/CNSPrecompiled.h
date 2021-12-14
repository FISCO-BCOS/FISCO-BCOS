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
 * @file CNSPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-27
 */

#pragma once
#include "../vm/Precompiled.h"
#include "Common.h"

#if 0
contract CNS
{
    function insert(string name, string version, Address addr, string abi) public returns(uint256);
    function selectByName(string name) public constant returns(string);
    function selectByNameAndVersion(string name, string version) public constant returns(string);
}
#endif

namespace bcos
{
const std::string SYS_CNS_FIELD_NAME = "name";
const std::string SYS_CNS_FIELD_VERSION = "version";
const std::string SYS_CNS_FIELD_ADDRESS = "address";
const std::string SYS_CNS_FIELD_ABI = "abi";

using CNSInfoMap = std::map<std::string, std::pair<std::string, std::string>>;

namespace precompiled
{
class CNSPrecompiled : public bcos::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<CNSPrecompiled> Ptr;
    CNSPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~CNSPrecompiled(){};

    int checkCNSParam(std::shared_ptr<executor::TransactionExecutive> _executive,
        std::string const& _contractAddress, std::string& _contractName,
        std::string& _contractVersion, std::string const& _contractAbi);

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;

private:
    void insert(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
    void selectByName(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
    void selectByNameAndVersion(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
    void getContractAddress(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
};
}  // namespace precompiled
}  // namespace bcos
