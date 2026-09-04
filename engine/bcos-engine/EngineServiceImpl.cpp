/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EngineServiceImpl.cpp
 */

#include "EngineServiceImpl.h"
#include "EngineServiceCommon.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <boost/throw_exception.hpp>
#include <span>
#include <stdexcept>
#include <utility>

// Leftover TU (findings BK / BW). Not compiled into libengine or
// test-bcos-engine. Production Engine API is EthEngineService / OpEngineService
// + engine_common. Do not port new consensus guards here. Delete with
// EngineServiceImpl.h once fixtures stop instantiating the leftover template.
std::vector<std::string> bcos::engine::detail::supportedCapabilities()
{
    return engine_common::supportedCapabilities();
}

bool bcos::engine::detail::isGetPayloadVersionCompatible(
    ApiVersion requestVersion, std::uint32_t payloadVersion)
{
    // Thin wrapper over engine_common::isGetPayloadVersionCompatible (finding AP):
    // identical window semantics; the historical rationale for each arm is documented
    // on the engine_common implementation. The parity test
    // engine_common_payload_version_matrix_matches_legacy pins the equivalence.
    return engine_common::isGetPayloadVersionCompatible(requestVersion, payloadVersion);
}

std::optional<std::string> bcos::engine::detail::validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version)
{
    // Thin wrapper over the shared engine_common rule (finding AP): the two bodies were
    // kept byte-identical and edited in lockstep; delegate so a future change has one
    // site. Behavior and messages are unchanged (engine_common_payload_version_matrix /
    // engine_common_validate_payload_attributes parity tests pin the equivalence).
    return engine_common::validatePayloadAttributes(payloadAttributes, version);
}
