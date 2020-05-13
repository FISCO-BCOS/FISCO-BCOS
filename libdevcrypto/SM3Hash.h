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
/** @file SM3Hash.h
 * @author xingqiangbai
 * @date 2020-04-27
 */

#pragma once

#include <libdevcore/FixedHash.h>
#include <libdevcore/vector_ref.h>
#include <string>

namespace dev
{
// SHA-3 convenience routines.

/// Calculate SM3-256 hash of the given input and load it into the given output.
/// @returns false if o_output.size() != 32.
bool sm3(bytesConstRef _input, bytesRef o_output);

/// Calculate SM3-256 hash of the given input, returning as a 256-bit hash.
inline h256 sm3(bytesConstRef _input)
{
    h256 ret;
    sm3(_input, ret.ref());
    return ret;
}
inline SecureFixedHash<32> sm3Secure(bytesConstRef _input)
{
    SecureFixedHash<32> ret;
    sm3(_input, ret.writable().ref());
    return ret;
}

/// Calculate SM3-256 hash of the given input, returning as a 256-bit hash.
inline h256 sm3(bytes const& _input)
{
    return sm3(bytesConstRef(&_input));
}
inline SecureFixedHash<32> sm3Secure(bytes const& _input)
{
    return sm3Secure(bytesConstRef(&_input));
}

/// Calculate SM3-256 hash of the given input (presented as a binary-filled string), returning as a
/// 256-bit hash.
inline h256 sm3(std::string const& _input)
{
    return sm3(bytesConstRef(_input));
}
inline SecureFixedHash<32> sm3Secure(std::string const& _input)
{
    return sm3Secure(bytesConstRef(_input));
}

/// Calculate SM3-256 hash of the given input (presented as a FixedHash), returns a 256-bit hash.
template <unsigned N>
inline h256 sm3(FixedHash<N> const& _input)
{
    return sm3(_input.ref());
}
template <unsigned N>
inline SecureFixedHash<32> sm3Secure(FixedHash<N> const& _input)
{
    return sm3Secure(_input.ref());
}

/// Fully secure variants are equivalent for sm3 and sm3Secure.
inline SecureFixedHash<32> sm3(bytesSec const& _input)
{
    return sm3Secure(_input.ref());
}
inline SecureFixedHash<32> sm3Secure(bytesSec const& _input)
{
    return sm3Secure(_input.ref());
}
template <unsigned N>
inline SecureFixedHash<32> sm3(SecureFixedHash<N> const& _input)
{
    return sm3Secure(_input.ref());
}
template <unsigned N>
inline SecureFixedHash<32> sm3Secure(SecureFixedHash<N> const& _input)
{
    return sm3Secure(_input.ref());
}

/// Calculate SM3-256 hash of the given input, possibly interpreting it as nibbles, and return the
/// hash as a string filled with binary data.
inline std::string sm3(std::string const& _input, bool _isNibbles)
{
    return asString((_isNibbles ? sm3(fromHex(_input)) : sm3(bytesConstRef(&_input))).asBytes());
}

/// Calculate SM3-256 MAC
inline void sm3mac(bytesConstRef _secret, bytesConstRef _plain, bytesRef _output)
{
    sm3(_secret.toBytes() + _plain.toBytes()).ref().populate(_output);
}

}  // namespace dev
