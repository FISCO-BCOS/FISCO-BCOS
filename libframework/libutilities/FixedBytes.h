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
 *  @brief: Implement the fixed-size bytes
 *  @file FixedBytes.h
 *  @date 2021-02-26
 */

#pragma once

#include "DataConvertUtility.h"
#include "Exceptions.h"
#include <boost/functional/hash.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>

namespace bcos
{
/// Compile-time calculation of Log2 of constant values.
template <unsigned N>
struct StaticLog2
{
    enum
    {
        result = 1 + StaticLog2<N / 2>::result
    };
};
template <>
struct StaticLog2<1>
{
    enum
    {
        result = 0
    };
};

extern std::random_device s_fixedBytesEngine;

/// Fixed-size raw-byte array container type, with an API optimised for storing hashes.
/// Transparently converts to/from the corresponding arithmetic type; this will
/// assume the data contained in the hash is big-endian.
template <unsigned N>
class FixedBytes
{
public:
    /// The corresponding arithmetic type.
    using ArithType = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<N * 8,
        N * 8, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
    enum
    {
        size = N
    };
    // construct FixedBytes from string
    enum StringDataType
    {
        FromHex,
        FromBinary
    };

    // align type when construct FixedBytes from the given bytes
    enum DataAlignType
    {
        AlignLeft,
        AlignRight,
        AcquireEqual
    };

    enum ConstructorType
    {
        FromPointer,
    };

    /**
     * @brief Construct a new empty Fixed Bytes object
     */
    FixedBytes() { m_data.fill(0); }

    /// Explicitly construct, copying from a byte array.
    explicit FixedBytes(
        bytesConstRef _bytesRefData, DataAlignType _alignType = DataAlignType::AlignRight)
      : FixedBytes()
    {
        constructFixedBytes(_bytesRefData, _alignType);
    }

    explicit FixedBytes(
        bytes const& _bytesData, DataAlignType _alignType = DataAlignType::AlignRight)
      : FixedBytes()
    {
        constructFixedBytes(bytesConstRef(_bytesData.data(), _bytesData.size()), _alignType);
    }

    /**
     * @brief Construct a new Fixed Bytes object from the given FixedBytes according to the align
     * type
     *
     * @tparam M: the size of the given FixedBytes
     * @param _fixedBytes: the given FixedBytes that used to construct the new FixedBytes
     * @param _alignType: the align type, support AlignLeft/AlignRight now
     */
    template <unsigned M>
    explicit FixedBytes(
        FixedBytes<M> const& _fixedBytes, DataAlignType _alignType = DataAlignType::AlignLeft)
    {
        m_data.fill(0);
        constructFixedBytes(_fixedBytes.ref(), _alignType);
    }

    /**
     * @brief Construct a new Fixed Bytes object according to the given number
     * @param _arithNumber: the number used to construct the new FixedBytes
     */
    FixedBytes(ArithType const& _arithNumber) : FixedBytes() { toBigEndian(_arithNumber, m_data); }
    FixedBytes(unsigned _arithNumber) : FixedBytes() { toBigEndian(_arithNumber, m_data); }

    explicit FixedBytes(byte const* _data, size_t _dataSize) : FixedBytes()
    {
        memcpy(m_data.data(), _data, std::min(_dataSize, (size_t)N));
    }

    explicit FixedBytes(byte const* _bs, ConstructorType) : FixedBytes()
    {
        memcpy(m_data.data(), _bs, N);
    }

    /// Explicitly construct, copying from a  string.
    explicit FixedBytes(std::string const& _s, StringDataType _t = FromHex,
        DataAlignType _alignType = DataAlignType::AlignRight)
      : FixedBytes(_t == FromHex ? *fromHexString(_s) : bcos::asBytes(_s), _alignType)
    {}

    // Convert to Arith type.
    operator ArithType() const { return fromBigEndian<ArithType>(m_data); }
    explicit operator bool() const
    {
        return std::any_of(m_data.begin(), m_data.end(), [](byte _b) { return _b != 0; });
    }
    bool operator==(FixedBytes const& _comparedFixedBytes) const
    {
        return m_data == _comparedFixedBytes.m_data;
    }
    bool operator!=(FixedBytes const& _comparedFixedBytes) const
    {
        return m_data != _comparedFixedBytes.m_data;
    }
    bool operator<(FixedBytes const& _comparedFixedBytes) const
    {
        for (unsigned index = 0; index < N; index++)
        {
            if (m_data[index] < _comparedFixedBytes[index])
            {
                return true;
            }
            else if (m_data[index] > _comparedFixedBytes[index])
            {
                return false;
            }
        }
        return false;
    }
    bool operator>=(FixedBytes const& _comparedFixedBytes) const
    {
        return !operator<(_comparedFixedBytes);
    }
    bool operator<=(FixedBytes const& _comparedFixedBytes) const
    {
        return operator==(_comparedFixedBytes) || operator<(_comparedFixedBytes);
    }
    bool operator>(FixedBytes const& _comparedFixedBytes) const
    {
        return !operator<=(_comparedFixedBytes);
    }
    FixedBytes& operator^=(FixedBytes const& _rightFixedBytes)
    {
        for (unsigned index = 0; index < N; index++)
        {
            m_data[index] ^= _rightFixedBytes.m_data[index];
        }
        return *this;
    }
    FixedBytes operator^(FixedBytes const& _rightFixedBytes) const
    {
        return FixedBytes(*this) ^= _rightFixedBytes;
    }
    FixedBytes& operator|=(FixedBytes const& _rightFixedBytes)
    {
        for (unsigned index = 0; index < N; index++)
        {
            m_data[index] |= _rightFixedBytes.m_data[index];
        }
        return *this;
    }
    FixedBytes operator|(FixedBytes const& _rightFixedBytes) const
    {
        return FixedBytes(*this) |= _rightFixedBytes;
    }
    FixedBytes& operator&=(FixedBytes const& _rightFixedBytes)
    {
        for (unsigned index = 0; index < N; index++)
        {
            m_data[index] &= _rightFixedBytes.m_data[index];
        }
        return *this;
    }
    FixedBytes operator&(FixedBytes const& _rightFixedBytes) const
    {
        return FixedBytes(*this) &= _rightFixedBytes;
    }
    FixedBytes operator~() const
    {
        FixedBytes result;
        for (unsigned index = 0; index < N; index++)
        {
            result[index] = ~m_data[index];
        }
        return result;
    }
    // @returns a specified byte from the FixedBytes
    byte& operator[](unsigned _index) { return m_data[_index]; }
    byte operator[](unsigned _index) const { return m_data[_index]; }

    // @returns an abridged version of the hash as a user-readable hex string
    std::string abridged() const { return *toHexString(ref().getCroppedData(0, 4)) + "..."; }
    /// @returns the hash as a user-readable hex string.
    std::string hex() const { return *toHexString(ref()); }
    /// @returns the hash as a user-readable hex string with 0x perfix.
    std::string hexPrefixed() const { return toHexStringWithPrefix(ref()); }

    /// @returns a mutable byte RefDataContainer to the object's data.
    bytesRef ref() { return bytesRef(m_data.data(), N); }
    /// @returns a constant byte RefDataContainer to the object's data.
    bytesConstRef ref() const { return bytesConstRef(m_data.data(), N); }
    /// @returns a mutable byte pointer to the object's data.
    byte* data() { return m_data.data(); }
    /// @returns a constant byte pointer to the object's data.
    byte const* data() const { return m_data.data(); }

    /// @returns begin iterator.
    auto begin() const -> typename std::array<byte, N>::const_iterator { return m_data.begin(); }
    /// @returns end iterator.
    auto end() const -> typename std::array<byte, N>::const_iterator { return m_data.end(); }
    /// @returns a copy of the object's data as a byte vector.
    bytes asBytes() const { return bytes(data(), data() + N); }

    /// Populate with random data.
    template <class Engine>
    void generateRandomFixedBytesByEngine(Engine& _eng)
    {
        std::uniform_int_distribution<uint8_t> dis(0, 255);
        for (auto& element : m_data)
        {
            element = dis(_eng);
        }
    }

    /// @returns a random valued object.
    static FixedBytes generateRandomFixedBytes()
    {
        FixedBytes randomFixedBytes;
        randomFixedBytes.generateRandomFixedBytesByEngine(s_fixedBytesEngine);
        return randomFixedBytes;
    }

    struct hash
    {
        /// Make a hash of the object's data.
        size_t operator()(FixedBytes const& _value) const
        {
            return boost::hash_range(_value.m_data.cbegin(), _value.m_data.cend());
        }
    };

    template <unsigned P, unsigned M>
    inline FixedBytes& shiftBloom(FixedBytes<M> const& _FixedBytes)
    {
        return (*this |= _FixedBytes.template bloomPart<P, N>());
    }

    template <unsigned P, unsigned M>
    inline FixedBytes<M> bloomPart() const
    {
        unsigned const c_bloomBits = M * 8;
        unsigned const c_mask = c_bloomBits - 1;
        unsigned const c_bloomBytes = (StaticLog2<c_bloomBits>::result + 7) / 8;

        static_assert((M & (M - 1)) == 0, "M must be power-of-two");
        static_assert(P * c_bloomBytes <= N, "out of range");

        FixedBytes<M> ret;
        byte const* p = data();
        for (unsigned i = 0; i < P; ++i)
        {
            unsigned index = 0;
            for (unsigned j = 0; j < c_bloomBytes; ++j, ++p)
                index = (index << 8) | *p;
            index &= c_mask;
            ret[M - 1 - index / 8] |= (1 << (index % 8));
        }
        return ret;
    }

    /// Returns the index of the first bit set to one, or size() * 8 if no bits are set.
    inline unsigned firstBitSet() const
    {
        unsigned ret = 0;
        for (auto d : m_data)
        {
            if (d)
            {
                for (;; ++ret, d <<= 1)
                {
                    if (d & 0x80)
                    {
                        return ret;
                    }
                }
            }
            else
            {
                ret += 8;
            }
        }
        return ret;
    }

    void clear() { m_data.fill(0); }

private:
    void constructFixedBytes(bytesConstRef _bytesData, DataAlignType _alignType)
    {
        m_data.fill(0);
        auto copyDataSize = std::min((unsigned)_bytesData.size(), N);
        switch (_alignType)
        {
        case DataAlignType::AlignLeft:
        {
            memcpy(m_data.data(), _bytesData.data(), copyDataSize);
            break;
        }
        case DataAlignType::AlignRight:
        {
            auto startIndex = N - copyDataSize;
            memcpy(m_data.data() + startIndex, _bytesData.data(), copyDataSize);
            break;
        }
        case DataAlignType::AcquireEqual:
        {
            if (_bytesData.size() != N)
            {
                BCOS_LOG(ERROR) << LOG_DESC("ConstructFixedBytesFailed") << LOG_KV("requiredLen", N)
                                << LOG_KV("dataLen", _bytesData.size());
                BOOST_THROW_EXCEPTION(ConstructFixedBytesFailed() << errinfo_comment(
                                          "Require " + std::to_string(N) + " length input data"));
            }
            memcpy(m_data.data(), _bytesData.data(), N);
            break;
        }
        }
    }

private:
    std::array<byte, N> m_data;  ///< The binary data.
};

template <unsigned T>
class SecureFixedBytes : private FixedBytes<T>
{
public:
    using DataAlignType = typename FixedBytes<T>::DataAlignType;
    using StringDataType = typename FixedBytes<T>::StringDataType;
    using ConstructorType = typename FixedBytes<T>::ConstructorType;
    SecureFixedBytes() = default;
    explicit SecureFixedBytes(
        bytesConstRef _fixedBytesRef, DataAlignType _alignType = DataAlignType::AlignRight)
      : FixedBytes<T>(_fixedBytesRef, _alignType)
    {}

    template <unsigned M>
    explicit SecureFixedBytes(
        FixedBytes<M> const& _fixedBytes, DataAlignType _alignType = DataAlignType::AlignLeft)
      : FixedBytes<T>(_fixedBytes, _alignType)
    {}
    template <unsigned M>
    explicit SecureFixedBytes(SecureFixedBytes<M> const& _secureFixedBytes,
        DataAlignType _alignType = DataAlignType::AlignLeft)
      : FixedBytes<T>(_secureFixedBytes.makeInsecure(), _alignType)
    {}
    explicit SecureFixedBytes(byte const* _bytesPtr, ConstructorType _type)
      : FixedBytes<T>(_bytesPtr, _type)
    {}
    explicit SecureFixedBytes(std::string const& _stringData,
        StringDataType _stringType = FixedBytes<T>::FromHex,
        DataAlignType _alignType = DataAlignType::AlignRight)
      : FixedBytes<T>(_stringData, _stringType, _alignType)
    {}
    SecureFixedBytes(SecureFixedBytes<T> const& _c) = default;
    ~SecureFixedBytes() { ref().cleanMemory(); }
    SecureFixedBytes<T>& operator=(SecureFixedBytes<T> const& _secureFixedBytes)
    {
        if (&_secureFixedBytes == this)
        {
            return *this;
        }
        ref().cleanMemory();
        FixedBytes<T>::operator=(static_cast<FixedBytes<T> const&>(_secureFixedBytes));
        return *this;
    }
    using FixedBytes<T>::size;

    FixedBytes<T> const& makeInsecure() const { return static_cast<FixedBytes<T> const&>(*this); }
    FixedBytes<T>& writable()
    {
        clear();
        return static_cast<FixedBytes<T>&>(*this);
    }

    using FixedBytes<T>::operator bool;

    // The obvious comparison operators.
    bool operator==(SecureFixedBytes const& _c) const
    {
        return static_cast<FixedBytes<T> const&>(*this).operator==(
            static_cast<FixedBytes<T> const&>(_c));
    }
    bool operator!=(SecureFixedBytes const& _c) const
    {
        return static_cast<FixedBytes<T> const&>(*this).operator!=(
            static_cast<FixedBytes<T> const&>(_c));
    }
    bool operator<(SecureFixedBytes const& _c) const
    {
        return static_cast<FixedBytes<T> const&>(*this).operator<(
            static_cast<FixedBytes<T> const&>(_c));
    }
    bool operator>=(SecureFixedBytes const& _c) const
    {
        return static_cast<FixedBytes<T> const&>(*this).operator>=(
            static_cast<FixedBytes<T> const&>(_c));
    }
    bool operator<=(SecureFixedBytes const& _c) const
    {
        return static_cast<FixedBytes<T> const&>(*this).operator<=(
            static_cast<FixedBytes<T> const&>(_c));
    }
    bool operator>(SecureFixedBytes const& _c) const
    {
        return static_cast<FixedBytes<T> const&>(*this).operator>(
            static_cast<FixedBytes<T> const&>(_c));
    }

    using FixedBytes<T>::operator==;
    using FixedBytes<T>::operator!=;
    using FixedBytes<T>::operator<;
    using FixedBytes<T>::operator>=;
    using FixedBytes<T>::operator<=;
    using FixedBytes<T>::operator>;

    // The obvious binary operators.
    SecureFixedBytes& operator^=(FixedBytes<T> const& _c)
    {
        static_cast<FixedBytes<T>&>(*this).operator^=(_c);
        return *this;
    }
    SecureFixedBytes operator^(FixedBytes<T> const& _c) const
    {
        return SecureFixedBytes(*this) ^= _c;
    }
    SecureFixedBytes& operator|=(FixedBytes<T> const& _c)
    {
        static_cast<FixedBytes<T>&>(*this).operator^=(_c);
        return *this;
    }
    SecureFixedBytes operator|(FixedBytes<T> const& _c) const
    {
        return SecureFixedBytes(*this) |= _c;
    }
    SecureFixedBytes& operator&=(FixedBytes<T> const& _c)
    {
        static_cast<FixedBytes<T>&>(*this).operator^=(_c);
        return *this;
    }
    SecureFixedBytes operator&(FixedBytes<T> const& _c) const
    {
        return SecureFixedBytes(*this) &= _c;
    }

    SecureFixedBytes& operator^=(SecureFixedBytes const& _c)
    {
        static_cast<FixedBytes<T>&>(*this).operator^=(static_cast<FixedBytes<T> const&>(_c));
        return *this;
    }
    SecureFixedBytes operator^(SecureFixedBytes const& _c) const
    {
        return SecureFixedBytes(*this) ^= _c;
    }
    SecureFixedBytes& operator|=(SecureFixedBytes const& _c)
    {
        static_cast<FixedBytes<T>&>(*this).operator^=(static_cast<FixedBytes<T> const&>(_c));
        return *this;
    }
    SecureFixedBytes operator|(SecureFixedBytes const& _c) const
    {
        return SecureFixedBytes(*this) |= _c;
    }
    SecureFixedBytes& operator&=(SecureFixedBytes const& _c)
    {
        static_cast<FixedBytes<T>&>(*this).operator^=(static_cast<FixedBytes<T> const&>(_c));
        return *this;
    }
    SecureFixedBytes operator&(SecureFixedBytes const& _c) const
    {
        return SecureFixedBytes(*this) &= _c;
    }
    SecureFixedBytes operator~() const
    {
        auto r = ~static_cast<FixedBytes<T> const&>(*this);
        return static_cast<SecureFixedBytes const&>(r);
    }

    using FixedBytes<T>::abridged;

    bytesConstRef ref() const { return FixedBytes<T>::ref(); }
    byte const* data() const { return FixedBytes<T>::data(); }

    static SecureFixedBytes<T> generateRandomFixedBytes()
    {
        SecureFixedBytes<T> randomFixedBytes;
        randomFixedBytes.generateRandomFixedBytesByEngine(s_fixedBytesEngine);
        return randomFixedBytes;
    }
    using FixedBytes<T>::firstBitSet;

    void clear() { ref().cleanMemory(); }
};

/// Fast equality operator for h256.
template <>
inline bool FixedBytes<32>::operator==(FixedBytes<32> const& _other) const
{
    const uint64_t* hash1 = (const uint64_t*)data();
    const uint64_t* hash2 = (const uint64_t*)_other.data();
    return (hash1[0] == hash2[0]) && (hash1[1] == hash2[1]) && (hash1[2] == hash2[2]) &&
           (hash1[3] == hash2[3]);
}

/// Fast std::hash compatible hash function object for h256.
template <>
inline size_t FixedBytes<32>::hash::operator()(FixedBytes<32> const& value) const
{
    uint64_t const* data = reinterpret_cast<uint64_t const*>(value.data());
    return boost::hash_range(data, data + 4);
}

/// Stream I/O for the FixedBytes class.
template <unsigned N>
inline std::ostream& operator<<(std::ostream& _out, FixedBytes<N> const& _h)
{
    _out << *toHexString(_h);
    return _out;
}

template <unsigned N>
inline std::istream& operator>>(std::istream& _in, FixedBytes<N>& o_h)
{
    std::string s;
    _in >> s;
    o_h = FixedBytes<N>(s, FixedBytes<N>::FromHex, FixedBytes<N>::AlignRight);
    return _in;
}

/// Stream I/O for the SecureFixedBytes class.
template <unsigned N>
inline std::ostream& operator<<(std::ostream& _out, SecureFixedBytes<N> const& _h)
{
    _out << "SecureFixedBytes#" << std::hex << typename FixedBytes<N>::hash()(_h.makeInsecure())
         << std::dec;
    return _out;
}

// Common types of FixedBytes.
using h2048 = FixedBytes<256>;
using h1024 = FixedBytes<128>;
using h520 = FixedBytes<65>;
using h512 = FixedBytes<64>;
using h256 = FixedBytes<32>;
using h160 = FixedBytes<20>;
using h128 = FixedBytes<16>;
using h64 = FixedBytes<8>;
using h512s = std::vector<h512>;
using h256s = std::vector<h256>;
using h160s = std::vector<h160>;
using h256Set = std::set<h256>;
using h160Set = std::set<h160>;
using h256Hash = std::unordered_set<h256>;
using h160Hash = std::unordered_set<h160>;

using Address = h160;
/// A vector of addresses.
using Addresses = h160s;
/// A hash set of addresses.
using AddressHash = std::unordered_set<h160>;
Address const ZeroAddress;

/// Convert the given value into h160 (160-bit unsigned integer) using the right 20 bytes.
inline h160 right160(h256 const& _t)
{
    h160 ret;
    memcpy(ret.data(), _t.data() + 12, 20);
    return ret;
}

/// Convert the given value into h160 (160-bit unsigned integer) using the left 20 bytes.
inline h160 left160(h256 const& _t)
{
    h160 ret;
    memcpy(&ret[0], _t.data(), 20);
    return ret;
}

inline std::string toString(h256s const& _bs)
{
    std::ostringstream out;
    out << "[ ";
    for (h256 const& i : _bs)
        out << i.abridged() << ", ";
    out << "]";
    return out.str();
}
// Convert from a 256-bit integer stack/memory entry into a 160-bit Address hash.
// Currently we just pull out the right (low-order in BE) 160-bits.
inline Address asAddress(u256 _item)
{
    return right160(h256(_item));
}

inline u256 fromAddress(Address _a)
{
    return (u160)_a;
}

inline Address toAddress(std::string const& _address)
{
    auto address = fromHexString(_address);
    if (address->size() == 20)
    {
        return Address(*address);
    }
    BOOST_THROW_EXCEPTION(InvalidAddress());
}
}  // namespace bcos

namespace std
{
template <>
struct hash<bcos::h160> : bcos::h160::hash
{
};
template <>
struct hash<bcos::h256> : bcos::h256::hash
{
};
template <>
struct hash<bcos::h512> : bcos::h512::hash
{
};
}  // namespace std
