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
 */

#pragma once

#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::engine
{
/// JSON-RPC -32603 "Internal error": an OP block execution failure the error-classification table
/// attributes to the storage layer rather than to the block. Must never be reported as INVALID --
/// a storage fault is not a consensus verdict on the payload. Lives in bcos-framework (not the
/// engine library) so opstack-executor can throw it without depending on bcos-engine.
DERIVE_BCOS_EXCEPTION(OpExecutionInternalError);

// Engine API exceptions (bcos-framework so RPC can map them without linking bcos-engine).
DERIVE_BCOS_EXCEPTION(UnsupportedEngineApiVersion);
DERIVE_BCOS_EXCEPTION(UnknownForkchoiceHeadBlock);
DERIVE_BCOS_EXCEPTION(InvalidForkchoiceState);
DERIVE_BCOS_EXCEPTION(InvalidPayloadAttributes);
DERIVE_BCOS_EXCEPTION(UnknownPayload);
DERIVE_BCOS_EXCEPTION(IncompatiblePayloadVersion);

/// JSON-RPC -38005 Unsupported fork. Isthmus+ requiring payload V4 is one use;
/// other fork-shape mismatches share this channel (see #5517).
DERIVE_BCOS_EXCEPTION(UnsupportedFork);

/// Structured carrier for the OP build-loop's poisoned-tx eviction: the OpScheduler
/// catch attaches the offending tx hash to the boundary bcos::Error as a typed
/// boost::error_info slot, and buildOpPayload reads it back to evict the culprit from
/// the pool — the same structured-member contract OpConsensusError documents at
/// opstack-executor/OpCommon.h. Never route this through the message text.
using OpCulpritTxHash = boost::error_info<struct OpCulpritTxHashTag, bcos::h256>;

}  // namespace bcos::engine
