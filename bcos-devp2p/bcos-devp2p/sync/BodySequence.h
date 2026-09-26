/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file BodySequence.h
 * @brief Block-body download and block assembly (eth/66+ GetBlockBodies).
 * @date 2026/8/18
 */
#pragma once

#include "Block.h"
#include "HeaderChain.h"
#include "../rlpx/Messages.h"
#include "../rlpx/Session.h"
#include "../eth/Protocol.h"

namespace bcos::devp2p::sync
{
// Fetches the bodies for a batch of headers and assembles complete blocks.
// eth/68 returns bodies in the same order as the requested hashes.
class BodySequence
{
public:
    // Fetch bodies for `_headers` (in order) and assemble complete blocks.
    // Throws on any protocol violation (request-id mismatch, wrong count).
    std::vector<Block> requestBodies(
        rlpx::Session& _session, std::vector<HeaderWithHash> const& _headers);

private:
    uint64_t m_requestId{0};
};
}  // namespace bcos::devp2p::sync
