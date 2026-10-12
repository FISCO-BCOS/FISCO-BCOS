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
 * @file T8nCorpus.h
 * @brief Shared constants of the external t8n corpus contract.
 */

#pragma once

#include <array>
#include <string_view>

namespace opstack_test
{
/// The two CRITICAL differential stems of the t8n corpus: the 8-fork synthetic ladder
/// (1000 blocks, regolith->jovian) and the real-derivation devnet snapshot. The stems
/// are derived by the producers (the generator prints LADDER-STEM; chainexport derives
/// digest8); this array is the ONE place the "critical" designation is pinned. Two
/// consumers enforce it: OpT8nReplayTest (each must be dispatched through the replay
/// loop) and OpGoldenCorpusProvenanceTest (each must be listed in the corpus'
/// vectors/SHA256SUMS with matching bytes, so a rewritten vector cannot shrink the
/// gate silently).
inline constexpr std::array<std::string_view, 2> c_t8nCriticalStems{
    "ladder_1000_69292b10", "devnet_1875-2274_c38db356"};

/// The corpus parent hash: the t8n corpus' genesis parent (env.parentHash of the
/// corpus' first vector) — the fixture anchor every suite that assembles
/// corpus-shaped headers/genesis announces (OpBlockVerifierTest, OpEthExecutorTest,
/// OpEthForkMatrixTest, OpSchedulerTest). Single-homed here so a corpus regeneration
/// that changes it breaks the build instead of silently forking per-suite literals.
/// FixedBytes has no string_view constructor: construct via
/// std::string(c_corpusParentHashHex).
inline constexpr std::string_view c_corpusParentHashHex{
    "0x45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74"};
}  // namespace opstack_test
