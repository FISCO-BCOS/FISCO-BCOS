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
 * @file VRFInfo.cpp
 * @author: kyonGuo
 * @date 2023/2/13
 */

#include "VRFInfo.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/Signature.h>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::precompiled;

bool VRFInfo::isValidVRFPublicKey()
{
    CInputBuffer rawPk{(const char*)m_vrfPublicKey.data(), m_vrfPublicKey.size()};
    return wedpr_curve25519_vrf_is_valid_public_key(&rawPk) == WEDPR_SUCCESS;
}

bool VRFInfo::verifyProof()
{
    CInputBuffer rawPk{(const char*)m_vrfPublicKey.data(), m_vrfPublicKey.size()};
    CInputBuffer rawInput{(const char*)m_vrfInput.data(), m_vrfInput.size()};
    CInputBuffer rawProof{(const char*)m_vrfProof.data(), m_vrfProof.size()};
    return (wedpr_curve25519_vrf_verify_utf8(&rawPk, &rawInput, &rawProof) == WEDPR_SUCCESS);
}

HashType VRFInfo::getHashFromProof()
{
    CInputBuffer rawProof{(const char*)m_vrfProof.data(), m_vrfProof.size()};
    HashType vrfHash;
    COutputBuffer outputHash{(char*)vrfHash.data(), vrfHash.size()};
    if (wedpr_curve25519_vrf_proof_to_hash(&rawProof, &outputHash) == WEDPR_SUCCESS)
    {
        return vrfHash;
    }
    return HashType{};
}
