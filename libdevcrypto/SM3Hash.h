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

#include <libutilities/FixedBytes.h>
#include <libutilities/vector_ref.h>
#include <string>

namespace bcos
{
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
inline SecureFixedBytes<32> sm3Secure(bytesConstRef _input)
{
    SecureFixedBytes<32> ret;
    sm3(_input, ret.writable().ref());
    return ret;
}

bool sm3(const uint8_t* _data, size_t _size, uint8_t* _hash);

/// Calculate SM3-256 hash of the given input, returning as a 256-bit hash.
inline h256 sm3(bytes const& _input)
{
    return sm3(bytesConstRef(&_input));
}
inline SecureFixedBytes<32> sm3Secure(bytes const& _input)
{
    return sm3Secure(bytesConstRef(&_input));
}

/// Calculate SM3-256 hash of the given input (presented as a binary-filled string), returning as a
/// 256-bit hash.
inline h256 sm3(std::string const& _input)
{
    return sm3(bytesConstRef(_input));
}
inline SecureFixedBytes<32> sm3Secure(std::string const& _input)
{
    return sm3Secure(bytesConstRef(_input));
}

/// Calculate SM3-256 hash of the given input (presented as a FixedBytes), returns a 256-bit hash.
template <unsigned N>
inline h256 sm3(FixedBytes<N> const& _input)
{
    return sm3(_input.ref());
}
template <unsigned N>
inline SecureFixedBytes<32> sm3Secure(FixedBytes<N> const& _input)
{
    return sm3Secure(_input.ref());
}

/// Fully secure variants are equivalent for sm3 and sm3Secure.
inline SecureFixedBytes<32> sm3(secBytes const& _input)
{
    return sm3Secure(_input.ref());
}
inline SecureFixedBytes<32> sm3Secure(secBytes const& _input)
{
    return sm3Secure(_input.ref());
}
template <unsigned N>
inline SecureFixedBytes<32> sm3(SecureFixedBytes<N> const& _input)
{
    return sm3Secure(_input.ref());
}
template <unsigned N>
inline SecureFixedBytes<32> sm3Secure(SecureFixedBytes<N> const& _input)
{
    return sm3Secure(_input.ref());
}

/// Calculate SM3-256 hash of the given input, possibly interpreting it as nibbles, and return the
/// hash as a string filled with binary data.
inline std::string sm3(std::string const& _input, bool _isNibbles)
{
    return asString(
        (_isNibbles ? sm3(*fromHexString(_input)) : sm3(bytesConstRef(&_input))).asBytes());
}

/// Calculate SM3-256 MAC
inline void sm3mac(bytesConstRef _secret, bytesConstRef _plain, bytesRef _output)
{
    sm3(_secret.toBytes() + _plain.toBytes()).ref().populate(_output);
}

}  // namespace bcos
