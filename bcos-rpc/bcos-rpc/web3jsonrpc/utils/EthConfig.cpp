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
 * @file EthConfig.cpp
 * @brief EIP-7910 `eth_config` builders.
 */
#include "EthConfig.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <array>
#include <cstddef>
#include <cstdio>

using namespace bcos;

namespace bcos::rpc
{
namespace
{
/// EIP-7910 precompile names (in activation order) with their fixed addresses.
struct Precompile
{
    char const* name;
    char const* address;  // 0x-prefixed, 20-byte lowercase
    evmc_revision since;  // first revision that activates it
};

constexpr std::array<Precompile, 17> c_precompiles{{
    {"ECREC", "0x0000000000000000000000000000000000000001", EVMC_FRONTIER},
    {"SHA256", "0x0000000000000000000000000000000000000002", EVMC_FRONTIER},
    {"RIPEMD160", "0x0000000000000000000000000000000000000003", EVMC_FRONTIER},
    {"ID", "0x0000000000000000000000000000000000000004", EVMC_FRONTIER},
    {"MODEXP", "0x0000000000000000000000000000000000000005", EVMC_BYZANTIUM},
    {"BN254_ADD", "0x0000000000000000000000000000000000000006", EVMC_BYZANTIUM},
    {"BN254_MUL", "0x0000000000000000000000000000000000000007", EVMC_BYZANTIUM},
    {"BN254_PAIRING", "0x0000000000000000000000000000000000000008", EVMC_BYZANTIUM},
    {"BLAKE2F", "0x0000000000000000000000000000000000000009", EVMC_ISTANBUL},
    {"KZG_POINT_EVALUATION", "0x000000000000000000000000000000000000000a", EVMC_CANCUN},
    {"BLS12_G1ADD", "0x000000000000000000000000000000000000000b", EVMC_PRAGUE},
    {"BLS12_G1MSM", "0x000000000000000000000000000000000000000c", EVMC_PRAGUE},
    {"BLS12_G2ADD", "0x000000000000000000000000000000000000000d", EVMC_PRAGUE},
    {"BLS12_G2MSM", "0x000000000000000000000000000000000000000e", EVMC_PRAGUE},
    {"BLS12_PAIRING_CHECK", "0x000000000000000000000000000000000000000f", EVMC_PRAGUE},
    {"BLS12_MAP_FP_TO_G1", "0x0000000000000000000000000000000000000010", EVMC_PRAGUE},
    {"BLS12_MAP_FP2_TO_G2", "0x0000000000000000000000000000000000000011", EVMC_PRAGUE},
}};

/// Fixed system-contract addresses (EIP-7910 / the defining EIPs), lowercase.
constexpr char const* c_beaconRoots = "0x000f3df6d732807ef1319fb7b8bb8522d0beac02";
constexpr char const* c_historyStorage = "0x0000f90827f1c53a10cb7a02335b175320002935";
constexpr char const* c_consolidationRequest = "0x0000bbddc7ce488642fb579f8b00f3a590007251";
constexpr char const* c_withdrawalRequest = "0x00000961ef480eb55e80d19ad83579a64c007002";
constexpr char const* c_depositContract = "0x00000000219ab540356cbb839cbe05303d7705fa";

/// IEEE CRC32 (the polynomial EIP-2124 uses).
uint32_t crc32Update(uint32_t crc, uint8_t const* data, std::size_t size)
{
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
        }
    }
    return ~crc;
}

/// Big-endian u64 (EIP-2124 appends fork blocks/times as 64-bit big-endian).
void appendU64Be(std::vector<uint8_t>& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
    }
}
}  // namespace

Json::Value ethConfigPrecompiles(evmc_revision revision)
{
    Json::Value result(Json::objectValue);
    for (auto const& pc : c_precompiles)
    {
        if (revision >= pc.since)
        {
            result[pc.name] = pc.address;
        }
    }
    return result;
}

Json::Value ethConfigSystemContracts(evmc_revision revision, bool opL2)
{
    Json::Value result(Json::objectValue);
    if (revision < EVMC_CANCUN)
    {
        return result;  // omitted for forks before Cancun
    }
    result["BEACON_ROOTS_ADDRESS"] = c_beaconRoots;
    if (revision < EVMC_PRAGUE)
    {
        return result;
    }
    result["HISTORY_STORAGE_ADDRESS"] = c_historyStorage;
    if (!opL2)
    {
        // The request predeploys and the deposit contract are L1-only.
        result["CONSOLIDATION_REQUEST_PREDEPLOY_ADDRESS"] = c_consolidationRequest;
        result["WITHDRAWAL_REQUEST_PREDEPLOY_ADDRESS"] = c_withdrawalRequest;
        result["DEPOSIT_CONTRACT_ADDRESS"] = c_depositContract;
    }
    return result;
}

Json::Value buildEthForkConfig(
    evmc_revision revision, uint64_t chainId, std::string_view forkIdHex, bool opL2)
{
    Json::Value config(Json::objectValue);
    // All FISCO / OP forks are active from genesis (chain-config isthmusTime=jovianTime=0).
    config["activationTime"] = Json::UInt64(0);
    Json::Value blobSchedule(Json::objectValue);
    if (revision >= EVMC_PRAGUE)
    {
        blobSchedule["baseFeeUpdateFraction"] = Json::UInt64(5007716);
        blobSchedule["max"] = Json::UInt64(9);
        blobSchedule["target"] = Json::UInt64(6);
    }
    else
    {
        // Cancun (and the default for earlier revisions whose blob params still apply).
        blobSchedule["baseFeeUpdateFraction"] = Json::UInt64(3338477);
        blobSchedule["max"] = Json::UInt64(6);
        blobSchedule["target"] = Json::UInt64(3);
    }
    config["blobSchedule"] = std::move(blobSchedule);
    config["chainId"] = toQuantity(chainId);
    config["forkId"] = std::string(forkIdHex);
    config["precompiles"] = ethConfigPrecompiles(revision);
    auto systemContracts = ethConfigSystemContracts(revision, opL2);
    if (!systemContracts.empty())
    {
        config["systemContracts"] = std::move(systemContracts);
    }
    return config;
}

Json::Value buildEthConfig(
    evmc_revision revision, uint64_t chainId, std::string_view forkIdHex, bool opL2)
{
    Json::Value result(Json::objectValue);
    result["current"] = buildEthForkConfig(revision, chainId, forkIdHex, opL2);
    // FISCO exposes no scheduled future fork and no preceding-fork history at the RPC layer.
    result["next"] = Json::nullValue;
    result["last"] = Json::nullValue;
    return result;
}

std::string ethForkIdHex(std::string_view genesisHashHex, std::vector<uint64_t> const& forks)
{
    if (genesisHashHex.empty())
    {
        return "0x00000000";
    }
    auto hex = std::string(genesisHashHex);
    if (hex.starts_with("0x") || hex.starts_with("0X"))
    {
        hex = hex.substr(2);
    }
    std::vector<uint8_t> input;
    input.reserve(hex.size() / 2 + forks.size() * 8);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        auto const byte = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
        input.push_back(byte);
    }
    for (auto fork : forks)
    {
        appendU64Be(input, fork);
    }
    auto const crc = crc32Update(0, input.data(), input.size());
    // EIP-2124: mask the high bit so the 32-bit checksum stays positive in JSON.
    auto const forkId = static_cast<uint32_t>(crc & 0x7fffffffu);
    std::array<char, 11> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "0x%08x", forkId);
    return std::string(buffer.data());
}
}  // namespace bcos::rpc
