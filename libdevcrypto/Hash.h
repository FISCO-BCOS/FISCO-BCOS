/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Hash.h
 * @author Alex Leverington <nessence@gmail.com> Asherli
 * @date 2018
 *
 * The FixedBytes fixed-size "hash" container type.
 */

#pragma once

#include <libutilities/Common.h>
#include <libutilities/FixedBytes.h>
#include <string>

namespace bcos
{
// Keccak256 convenience routines.

/// Calculate Keccak256 hash of the given input and load it into the given output.
/// @returns false if o_output.size() != 32.
bool keccak256(bytesConstRef _input, bytesRef o_output);

// secp256k1_sha256
h256 standardSha256(bytesConstRef _input) noexcept;

// sha2 - sha256 replace Hash.h begin
h256 sha256(bytesConstRef _input) noexcept;

h160 ripemd160(bytesConstRef _input);

/// Calculate Keccak256 hash of the given input, returning as a 256-bit hash.
inline h256 keccak256(bytesConstRef _input)
{
    h256 ret;
    keccak256(_input, ret.ref());
    return ret;
}
inline SecureFixedBytes<32> keccak256Secure(bytesConstRef _input)
{
    SecureFixedBytes<32> ret;
    keccak256(_input, ret.writable().ref());
    return ret;
}

/// Calculate Keccak256 hash of the given input, returning as a 256-bit hash.
inline h256 keccak256(bytes const& _input)
{
    return keccak256(bytesConstRef(&_input));
}
inline SecureFixedBytes<32> keccak256Secure(bytes const& _input)
{
    return keccak256Secure(bytesConstRef(&_input));
}

/// Calculate Keccak256 hash of the given input (presented as a binary-filled string), returning as
/// a 256-bit hash.
inline h256 keccak256(std::string const& _input)
{
    return keccak256(bytesConstRef(_input));
}
inline SecureFixedBytes<32> keccak256Secure(std::string const& _input)
{
    return keccak256Secure(bytesConstRef(_input));
}

/// Calculate Keccak256 hash of the given input (presented as a FixedBytes), returns a 256-bit hash.
template <unsigned N>
inline h256 keccak256(FixedBytes<N> const& _input)
{
    return keccak256(_input.ref());
}
template <unsigned N>
inline SecureFixedBytes<32> keccak256Secure(FixedBytes<N> const& _input)
{
    return keccak256Secure(_input.ref());
}

template <unsigned N>
inline SecureFixedBytes<32> keccak256(SecureFixedBytes<N> const& _input)
{
    return keccak256Secure(_input.ref());
}
template <unsigned N>
inline SecureFixedBytes<32> keccak256Secure(SecureFixedBytes<N> const& _input)
{
    return keccak256Secure(_input.ref());
}

/// Calculate Keccak256 hash of the given input, possibly interpreting it as nibbles, and return the
/// hash as a string filled with binary data.
inline std::string keccak256(std::string const& _input, bool _isNibbles)
{
    return asString(
        (_isNibbles ? keccak256(*fromHexString(_input)) : keccak256(bytesConstRef(&_input)))
            .asBytes());
}

/// Calculate Keccak256 MAC
inline void keccak256mac(bytesConstRef _secret, bytesConstRef _plain, bytesRef _output)
{
    keccak256(_secret.toBytes() + _plain.toBytes()).ref().populate(_output);
}

}  // namespace bcos
