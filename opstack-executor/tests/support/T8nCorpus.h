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
#include <cstdint>
#include <string_view>

namespace opstack_test
{
/// The two CRITICAL differential stems of the t8n corpus: the 8-fork synthetic ladder
/// (1000 blocks, regolith->jovian) and the real-derivation devnet snapshot. The stems
/// are derived by the producers (the generator prints LADDER-STEM; chainexport derives
/// digest8); this array is the ONE place the "critical" designation is pinned. Its two
/// consumers ride in #5687 (B2) and enforce it once that PR restacks on this one — no
/// consumer exists in THIS tree (review round-1 F5): OpT8nReplayTest (each stem must be
/// dispatched through the replay loop) and OpGoldenCorpusProvenanceTest (each must be
/// listed in the corpus' vectors/SHA256SUMS with matching bytes, so a rewritten vector
/// cannot shrink the gate silently).
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

/// The FISCO OP chain id (8453 = 0x2105) the t8n corpus envelopes embed; every suite that
/// builds an envelope or an execution context announces it from here. Single home for the
/// numeric constant: review F42 folded the fixture-side copies (DualRunHarness.h,
/// OpEngineE2eFixture.h, OpBlockVerifierTest.cpp), and the two remaining suites
/// (OpSchedulerTest, OpEngineServiceExecParityTest) alias this constant rather than
/// re-pin the value. The decimal "8453" tars strings stay local to their call sites.
inline constexpr uint64_t c_opChainId = 0x2105;
}  // namespace opstack_test
