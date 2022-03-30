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
 * @file FileSystemPrecompiled.h
 * @author: kyonRay
 * @date 2021-06-10
 */

#pragma once
#include "../vm/Precompiled.h"

namespace bcos::precompiled
{
class FileSystemPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<FileSystemPrecompiled>;
    FileSystemPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~FileSystemPrecompiled() = default;
    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender, int64_t gasLeft) override;
    s256 externalTouchNewFile(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _origin, const std::string& _sender, const std::string& _receiver,
        const std::string& _filePath, const std::string& _fileType, int64_t gasLeft);

private:
    void listDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult,
        const PrecompiledGas::Ptr& gasPricer);
    void makeDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult,
        const std::string& _origin, const PrecompiledGas::Ptr& gasPricer, int64_t gasLeft);
    void link(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult,
        const std::string& _origin, int64_t gasLeft);
    void readLink(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult);
    void touch(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult);
    int checkLinkParam(std::shared_ptr<executor::TransactionExecutive> _executive,
        std::string const& _contractAddress, std::string& _contractName,
        std::string& _contractVersion, std::string const& _contractAbi);
    std::set<std::string> BfsTypeSet;
};
}  // namespace bcos::precompiled