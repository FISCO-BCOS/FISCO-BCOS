/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file VRFInfo.h
 * @author: kyonGuo
 * @date 2023/2/13
 */

#pragma once
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include <bcos-framework/storage/Table.h>

namespace bcos::precompiled
{
class VRFInfo
{
public:
    using Ptr = std::shared_ptr<VRFInfo>;
    VRFInfo(std::string _vrfProof, std::string _vrfPk, std::string _vrfInput)
      : m_vrfProof(std::move(_vrfProof)),
        m_vrfPublicKey(std::move(_vrfPk)),
        m_vrfInput(std::move(_vrfInput))
    {}

    ~VRFInfo() = default;
    bool verifyProof();
    bcos::crypto::HashType getHashFromProof();
    bool isValidVRFPublicKey();

    std::string const& vrfProof() const { return m_vrfProof; }
    std::string const& vrfPublicKey() const { return m_vrfPublicKey; }
    std::string const& vrfInput() const { return m_vrfInput; }

private:
    std::string m_vrfProof;
    std::string m_vrfPublicKey;
    std::string m_vrfInput;
};
}  // namespace bcos::precompiled