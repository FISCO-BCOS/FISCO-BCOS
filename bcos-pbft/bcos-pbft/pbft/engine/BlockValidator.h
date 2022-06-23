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
 * @brief validator for the block from the sync module
 * @file BlockValidator.h
 * @author: yujiechen
 * @date 2021-05-25
 */
#pragma once
#include "../config/PBFTConfig.h"
#include <bcos-framework//protocol/Block.h>
#include <bcos-utilities/ThreadPool.h>
namespace bcos
{
namespace consensus
{
class BlockValidator : public std::enable_shared_from_this<BlockValidator>
{
public:
    using Ptr = std::shared_ptr<BlockValidator>;
    explicit BlockValidator(PBFTConfig::Ptr _config)
      : m_config(_config), m_taskPool(std::make_shared<ThreadPool>("blockValidator", 1))
    {}
    virtual ~BlockValidator() {}

    virtual void asyncCheckBlock(
        bcos::protocol::Block::Ptr _block, std::function<void(Error::Ptr, bool)> _onVerifyFinish);

    virtual void stop()
    {
        if (m_taskPool)
        {
            m_taskPool->stop();
        }
    }

protected:
    virtual bool checkSealerListAndWeightList(bcos::protocol::Block::Ptr _block);
    virtual bool checkSignatureList(bcos::protocol::Block::Ptr _block);

private:
    PBFTConfig::Ptr m_config;
    ThreadPool::Ptr m_taskPool;
};
}  // namespace consensus
}  // namespace bcos