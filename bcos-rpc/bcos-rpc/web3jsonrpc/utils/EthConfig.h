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
 * @file EthConfig.h
 * @brief EIP-7910 `eth_config` builders (fork configuration JSON-RPC method).
 */
#pragma once

#include <evmc/evmc.h>
#include <json/json.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::rpc
{

/// EIP-7910 `precompiles`: the active precompile set for the revision, keyed by the agreed
/// name (e.g. "ECREC") with the 0x-prefixed 20-byte address as the value.
Json::Value ethConfigPrecompiles(evmc_revision revision);

/// EIP-7910 `systemContracts`: the system-level contracts active for the revision, keyed by
/// the defining EIP's name (e.g. "BEACON_ROOTS_ADDRESS"). On an OP L2 (opL2) only the
/// L2-relevant set (beacon roots + history storage) is reported; the L1-only request
/// predeploys and the deposit contract are omitted.
Json::Value ethConfigSystemContracts(evmc_revision revision, bool opL2);

/// EIP-7910 one `EthForkConfig` object for the given revision / chain id / fork id.
/// `forkIdHex` is the 4-byte 0x-prefixed EIP-2124 fork id (e.g. "0x0929e24e").
Json::Value buildEthForkConfig(
    evmc_revision revision, bcos::u256 chainId, std::string_view forkIdHex, bool opL2);

/// EIP-7910 `eth_config` result: {"current": <EthForkConfig>, "next": null, "last": null}.
/// FISCO has no scheduled future fork and no timestamp-activated history at the RPC layer,
/// so `next`/`last` are null and `current.activationTime` is the genesis time (0).
Json::Value buildEthConfig(
    evmc_revision revision, bcos::u256 chainId, std::string_view forkIdHex, bool opL2);

/// EIP-2124 fork id: full IEEE CRC32 over the genesis hash (and, when the fork list is
/// non-empty, each fork block as big-endian u64) — the FULL 32 bits, high bit included;
/// geth fork ids carry it (mainnet genesis 0xfc64ec04). Empty genesis hash -> "0x00000000".
std::string ethForkIdHex(std::string_view genesisHashHex, std::vector<uint64_t> const& forks);

}  // namespace bcos::rpc
