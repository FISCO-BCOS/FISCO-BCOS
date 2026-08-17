/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file ChecksumAddress.h
 * @author: xingqiangbai
 * @date: 2021-07-30
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <string>
#include <string_view>

namespace bcos
{
void toChecksumAddress(
    std::string& _addr, const std::string_view& addressHashHex, std::string_view prefix = "");

void toCheckSumAddress(std::string& _hexAddress, crypto::Hash::Ptr _hashImpl);

// for EIP-1191, hexAdress input should NOT have prefix "0x"
void toCheckSumAddressWithChainId(
    std::string& _hexAddress, crypto::Hash::Ptr _hashImpl, uint64_t _chainId = 0);

void toAddress(std::string& _hexAddress);

std::string toChecksumAddressFromBytes(
    const std::string_view& _AddressBytes, crypto::Hash::Ptr _hashImpl);

// address based on blockNumber, contextID, seq
std::string newEVMAddress(
    const bcos::crypto::Hash& _hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq);

std::string newEVMAddress(
    const bcos::crypto::Hash::Ptr& _hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq);

// keccak256(rlp.encode([normalize_address(sender), nonce]))[12:]
evmc_address newLegacyEVMAddress(bytesConstRef sender, const u256& nonce) noexcept;

std::string newLegacyEVMAddressString(bytesConstRef sender, const u256& nonce) noexcept;

std::string newLegacyEVMAddressString(bytesConstRef sender, std::string const& nonce) noexcept;

// EIP-1014
std::string newCreate2EVMAddress(bcos::crypto::Hash::Ptr _hashImpl,
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt);

}  // namespace bcos
