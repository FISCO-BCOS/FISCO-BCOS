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
 * @brief interface for PBFT request
 * @file PBFTRequestInterface.h
 * @author: yujiechen
 * @date 2021-04-28
 */
#pragma once
#include "PBFTBaseMessageInterface.h"
namespace bcos
{
namespace consensus
{
class PBFTRequestInterface : virtual public PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<PBFTRequestInterface>;
    PBFTRequestInterface() = default;
    virtual ~PBFTRequestInterface() {}

    virtual void setSize(int64_t _size) = 0;
    virtual int64_t size() const = 0;
};
}  // namespace consensus
}  // namespace bcos