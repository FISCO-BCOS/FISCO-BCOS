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
 * @file BFSPrecompiled.h
 * @author: kyonRay
 * @date 2021-06-10
 */

#pragma once
#include "../vm/Precompiled.h"

namespace bcos::precompiled
{
class BFSPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<BFSPrecompiled>;
    BFSPrecompiled(crypto::Hash::Ptr _hashImpl);
    ~BFSPrecompiled() override = default;
    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void listDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void listDirPage(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void makeDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void link(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void linkAdaptCNS(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void readLink(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void touch(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void initBfs(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void rebuildBfs(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void rebuildBfs310(const std::shared_ptr<executor::TransactionExecutive>& _executive);
    void fixBfs(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void fixBfs330(const std::shared_ptr<executor::TransactionExecutive>& _executive);
    int checkLinkParam(std::shared_ptr<executor::TransactionExecutive> _executive,
        std::string const& _contractAddress, std::string& _contractName,
        std::string& _contractVersion, std::string const& _contractAbi);
    bool recursiveBuildDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _absoluteDir);
    std::set<std::string> BfsTypeSet;

protected:
    void makeDirImpl(const std::string& _absolutePath,
        const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void linkImpl(const std::string& _absolutePath, const std::string& _contractAddress,
        const std::string& _contractAbi,
        const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    virtual const char* getThisAddress(bool _isWasm) { return _isWasm ? BFS_NAME : BFS_ADDRESS; }
    virtual std::string_view getLinkRootDir() { return executor::USER_APPS_PREFIX; }
    virtual bool checkPathPrefixValid(
        const std::string_view& path, uint32_t blockVersion, const std::string_view& type);

    inline bool isShardPath(const std::string& _path)
    {
        return _path.starts_with(executor::USER_SHARD_PREFIX);
    }
};
}  // namespace bcos::precompiled