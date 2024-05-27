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
    VRFInfo(bcos::bytes _vrfProof, bcos::bytes _vrfPk, bcos::bytes _vrfInput)
      : m_vrfProof(std::move(_vrfProof)),
        m_vrfPublicKey(std::move(_vrfPk)),
        m_vrfInput(std::move(_vrfInput))
    {}

    ~VRFInfo() = default;
    bool verifyProof();
    bcos::crypto::HashType getHashFromProof();
    bool isValidVRFPublicKey();

    bcos::bytes const& vrfProof() const { return m_vrfProof; }
    bcos::bytes const& vrfPublicKey() const { return m_vrfPublicKey; }
    bcos::bytes const& vrfInput() const { return m_vrfInput; }

private:
    bcos::bytes m_vrfProof;
    bcos::bytes m_vrfPublicKey;
    bcos::bytes m_vrfInput;
};
}  // namespace bcos::precompiled