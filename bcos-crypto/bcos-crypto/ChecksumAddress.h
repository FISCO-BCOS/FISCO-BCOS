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

#include <bcos-codec/bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <fmt/format.h>
#include <boost/algorithm/string.hpp>
#include <string>

namespace bcos
{
inline void toChecksumAddress(
    std::string& _addr, const std::string_view& addressHashHex, std::string_view prefix = "")
{
    auto convertHexCharToInt = [](char byte) {
        int ret = 0;
        if (byte >= '0' && byte <= '9')
        {
            ret = byte - '0';
        }
        else if (byte >= 'a' && byte <= 'f')
        {
            ret = byte - 'a' + 10;
        }
        else if (byte >= 'A' && byte <= 'F')
        {
            ret = byte - 'A' + 10;
        }
        return ret;
    };
    for (size_t i = prefix.size(); i < _addr.size(); ++i)
    {
        if (isdigit(_addr[i]))
        {
            continue;
        }
        if (convertHexCharToInt(addressHashHex[i]) >= 8)
        {
            _addr[i] = toupper(_addr[i]);
        }
    }
}

inline void toCheckSumAddress(std::string& _hexAddress, crypto::Hash::Ptr _hashImpl)
{
    boost::algorithm::to_lower(_hexAddress);
    toChecksumAddress(_hexAddress, _hashImpl->hash(_hexAddress).hex());
}

// for EIP-1191, hexAdress input should NOT have prefix "0x"
inline void toCheckSumAddressWithChainId(
    std::string& _hexAddress, crypto::Hash::Ptr _hashImpl, uint64_t _chainId = 0)
{
    boost::algorithm::to_lower(_hexAddress);
    std::string hashInput = _hexAddress;
    if (_chainId != 0 && _chainId != 1)
    {
        hashInput = fmt::format("{}0x{}", _chainId, _hexAddress);
    }
    toChecksumAddress(_hexAddress, _hashImpl->hash(hashInput).hex());
}

inline void toAddress(std::string& _hexAddress)
{
    boost::algorithm::to_lower(_hexAddress);
    // toChecksumAddress(_hexAddress, _hashImpl->hash(_hexAddress).hex()); notice :
    // toChecksumAddress must be used before rpc return
}

inline std::string toChecksumAddressFromBytes(
    const std::string_view& _AddressBytes, crypto::Hash::Ptr _hashImpl)
{
    auto hexAddress = *toHexString(_AddressBytes);
    toAddress(hexAddress);
    return hexAddress;
}

// address based on blockNumber, contextID, seq
inline std::string newEVMAddress(
    const bcos::crypto::Hash& _hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq)
{
    auto hash = _hashImpl.hash(boost::lexical_cast<std::string>(blockNumber) + "_" +
                               boost::lexical_cast<std::string>(contextID) + "_" +
                               boost::lexical_cast<std::string>(seq));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toAddress(hexAddress);

    return hexAddress;
}

inline std::string newEVMAddress(
    const bcos::crypto::Hash::Ptr& _hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq)
{
    return newEVMAddress(*_hashImpl, blockNumber, contextID, seq);
}


// keccak256(rlp.encode([normalize_address(sender), nonce]))[12:]
inline std::string newLegacyEVMAddress(bytesConstRef sender, u256 nonce) noexcept
{
    codec::rlp::Header header{true, 1 + sender.size()};
    header.payloadLength += codec::rlp::length(nonce);
    bcos::bytes rlp;
    codec::rlp::encodeHeader(rlp, header);
    codec::rlp::encode(rlp, sender);
    codec::rlp::encode(rlp, nonce);
    auto hash = bcos::crypto::keccak256Hash(ref(rlp));
    std::string out;
    boost::algorithm::hex_lower(hash.begin() + 12, hash.end(), std::back_inserter(out));
    return out;
}

inline std::string newLegacyEVMAddress(bytesConstRef sender, std::string const& nonce) noexcept
{
    const auto uNonce = hex2u(nonce);
    return newLegacyEVMAddress(sender, uNonce);
}

// EIP-1014
inline std::string newCreate2EVMAddress(bcos::crypto::Hash::Ptr _hashImpl,
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash = _hashImpl->hash(
        bytes{0xff} + (_sender.starts_with("0x") ? fromHexWithPrefix(_sender) : fromHex(_sender)) +
        toBigEndian(_salt) + _hashImpl->hash(_init));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data() + 12, hash.data() + 32, std::back_inserter(hexAddress));

    toAddress(hexAddress);

    return hexAddress;
}

}  // namespace bcos
