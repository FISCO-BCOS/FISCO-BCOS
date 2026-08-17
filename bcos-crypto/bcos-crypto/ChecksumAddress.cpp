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
 */

#include <bcos-crypto/ChecksumAddress.h>

#include <bcos-codec/bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <fmt/format.h>
#include <cctype>
#include <memory>
#include <span>

namespace
{
int convertHexCharToInt(char byte)
{
    if (byte >= '0' && byte <= '9')
    {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f')
    {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F')
    {
        return byte - 'A' + 10;
    }
    return 0;
}
}  // namespace

namespace bcos
{
void toChecksumAddress(
    std::string& _addr, const std::string_view& addressHashHex, std::string_view prefix)
{
    for (size_t i = prefix.size(); i < _addr.size(); ++i)
    {
        if (std::isdigit(static_cast<unsigned char>(_addr[i])))
        {
            continue;
        }
        if (convertHexCharToInt(addressHashHex[i]) >= 8)
        {
            _addr[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(_addr[i])));
        }
    }
}

void toCheckSumAddress(std::string& _hexAddress, crypto::Hash::Ptr _hashImpl)
{
    boost::algorithm::to_lower(_hexAddress);
    toChecksumAddress(_hexAddress, _hashImpl->hash(_hexAddress).hex());
}

void toCheckSumAddressWithChainId(
    std::string& _hexAddress, crypto::Hash::Ptr _hashImpl, uint64_t _chainId)
{
    boost::algorithm::to_lower(_hexAddress);
    std::string hashInput = _hexAddress;
    if (_chainId != 0 && _chainId != 1)
    {
        hashInput = fmt::format("{}0x{}", _chainId, _hexAddress);
    }
    toChecksumAddress(_hexAddress, _hashImpl->hash(hashInput).hex());
}

void toAddress(std::string& _hexAddress)
{
    boost::algorithm::to_lower(_hexAddress);
}

std::string toChecksumAddressFromBytes(
    const std::string_view& _AddressBytes, crypto::Hash::Ptr _hashImpl)
{
    auto hexAddress = toHex(_AddressBytes);
    toAddress(hexAddress);
    return hexAddress;
}

std::string newEVMAddress(
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

std::string newEVMAddress(
    const bcos::crypto::Hash::Ptr& _hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq)
{
    return newEVMAddress(*_hashImpl, blockNumber, contextID, seq);
}

evmc_address newLegacyEVMAddress(bytesConstRef sender, const u256& nonce) noexcept
{
    codec::rlp::Header header{.isList = true, .payloadLength = 1 + sender.size()};
    header.payloadLength += codec::rlp::length(nonce);
    bcos::bytes rlp;
    codec::rlp::encodeHeader(rlp, header);
    codec::rlp::encode(rlp, sender);
    codec::rlp::encode(rlp, nonce);
    auto hash = bcos::crypto::keccak256Hash(ref(rlp));
    evmc_address address;
    std::uninitialized_copy(hash.begin() + 12, hash.end(), address.bytes);

    return address;
}

std::string newLegacyEVMAddressString(bytesConstRef sender, const u256& nonce) noexcept
{
    auto address = newLegacyEVMAddress(sender, nonce);
    auto view = std::span{address.bytes};
    std::string out;
    out.reserve(view.size() * 2);
    boost::algorithm::hex_lower(view.begin(), view.end(), std::back_inserter(out));
    return out;
}

std::string newLegacyEVMAddressString(bytesConstRef sender, std::string const& nonce) noexcept
{
    const auto uNonce = hex2u(nonce);
    return newLegacyEVMAddressString(sender, uNonce);
}

std::string newCreate2EVMAddress(bcos::crypto::Hash::Ptr _hashImpl,
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash = _hashImpl->hash(bytes{0xff} + fromHex(_sender) + toBigEndian(_salt) +
                                _hashImpl->hash(_init));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data() + 12, hash.data() + 32, std::back_inserter(hexAddress));

    toAddress(hexAddress);

    return hexAddress;
}
}  // namespace bcos