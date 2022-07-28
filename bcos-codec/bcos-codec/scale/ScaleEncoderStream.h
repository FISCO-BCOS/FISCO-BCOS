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
 * @brief scale encoder
 * @file ScaleEncoderStream.cpp
 */
#pragma once
#include "FixedWidthIntegerCodec.h"
#include <bcos-utilities/FixedBytes.h>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <deque>
#include <gsl/span>
#include <type_traits>

namespace bcos
{
namespace codec
{
namespace scale
{
class ScaleEncoderStream
{
public:
    // special tag to differentiate encoding streams from others
    static constexpr auto is_encoder_stream = true;

    // get the encoded data
    bytes data() const;

    /**
     * @brief scale-encodes pair of values
     * @tparam F first value type
     * @tparam S second value type
     * @param p pair of values to encode
     * @return reference to stream
     */
    template <class F, class S>
    ScaleEncoderStream& operator<<(const std::pair<F, S>& p)
    {
        return *this << p.first << p.second;
    }

    /**
     * @brief scale-encodes tuple
     * @tparam T enumeration of types
     * @param v tuple
     * @return reference to stream
     */
    template <class... Ts>
    ScaleEncoderStream& operator<<(const std::tuple<Ts...>& v)
    {
        if constexpr (sizeof...(Ts) > 0)
        {
            encodeElementOfTuple<0>(v);
        }
        return *this;
    }

    /**
     * @brief scale-encodes variant value
     * @tparam T type list
     * @param v value to encode
     * @return reference to stream
     */
    template <class... T>
    ScaleEncoderStream& operator<<(const boost::variant<T...>& v)
    {
        tryEncodeAsOneOfVariant<0>(v);
        return *this;
    }

    /**
     * @brief scale-encodes sharead_ptr value
     * @tparam T type list
     * @param v value to encode
     * @return reference to stream
     */
    template <class T>
    ScaleEncoderStream& operator<<(const std::shared_ptr<T>& v)
    {
        if (v == nullptr)
        {
            BOOST_THROW_EXCEPTION(ScaleEncodeException()
                                  << errinfo_comment("encode exception for DEREF_NULLPOINTER"));
        }
        return *this << *v;
    }

    /**
     * @brief scale-encodes unique_ptr value
     * @tparam T type list
     * @param v value to encode
     * @return reference to stream
     */
    template <class T>
    ScaleEncoderStream& operator<<(const std::unique_ptr<T>& v)
    {
        if (v == nullptr)
        {
            BOOST_THROW_EXCEPTION(ScaleEncodeException()
                                  << errinfo_comment("encode exception for DEREF_NULLPOINTER"));
        }
        return *this << *v;
    }

    template <unsigned N>
    ScaleEncoderStream& operator<<(const FixedBytes<N>& fixedData)
    {
        for (auto it = fixedData.begin(); it != fixedData.end(); ++it)
        {
            *this << *it;
        }
        return *this;
    }

    /**
     * @brief scale-encodes collection of same time items
     * @tparam T type of item
     * @param c collection to encode
     * @return reference to stream
     */
    template <class T>
    ScaleEncoderStream& operator<<(const std::vector<T>& c)
    {
        return encodeCollection(c.size(), c.begin(), c.end());
    }

    /**
     * @brief scale-encodes collection of same time items
     * @tparam T type of item
     * @param c collection to encode
     * @return reference to stream
     */
    template <class T>
    ScaleEncoderStream& operator<<(const std::list<T>& c)
    {
        return encodeCollection(c.size(), c.begin(), c.end());
    }

    /**
     * @brief scale-encodes collection of map
     * @tparam T type of item
     * @tparam F type of item
     * @param c collection to encode
     * @return reference to stream
     */
    template <class T, class F>
    ScaleEncoderStream& operator<<(const std::map<T, F>& c)
    {
        return encodeCollection(c.size(), c.begin(), c.end());
    }

    /**
     * @brief scale-encodes optional value
     * @tparam T value type
     * @param v value to encode
     * @return reference to stream
     */
    template <class T>
    ScaleEncoderStream& operator<<(const boost::optional<T>& v)
    {
        // optional bool is a special case of optional values
        // it should be encoded using one byte instead of two
        // as described in specification
        if constexpr (std::is_same<T, bool>::value)
        {
            return encodeOptionalBool(v);
        }
        if (!v.has_value())
        {
            return putByte(0u);
        }
        return putByte(1u) << *v;
    }

    /**
     * @brief appends sequence of bytes
     * @param v bytes sequence
     * @return reference to stream
     */
    template <class T>
    ScaleEncoderStream& operator<<(const gsl::span<T>& v)
    {
        return encodeCollection(v.size(), v.begin(), v.end());
    }

    /**
     * @brief scale-encodes array of items
     * @tparam T item type
     * @tparam size of the array
     * @param a reference to the array
     * @return reference to stream
     */
    template <typename T, size_t size>
    ScaleEncoderStream& operator<<(const std::array<T, size>& a)
    {
        for (const auto& e : a)
        {
            *this << e;
        }
        return *this;
    }

    /**
     * @brief scale-encodes std::reference_wrapper of a type
     * @tparam T underlying type
     * @param v value to encode
     * @return reference to stream;
     */
    template <class T>
    ScaleEncoderStream& operator<<(const std::reference_wrapper<T>& v)
    {
        return *this << static_cast<const T&>(v);
    }

    /**
     * @brief scale-encodes a string view
     * @param sv string_view item
     * @return reference to stream
     */
    ScaleEncoderStream& operator<<(std::string_view sv)
    {
        return encodeCollection(sv.size(), sv.begin(), sv.end());
    }

    /**
     * @brief scale-encodes any integral type including bool
     * @tparam T integral type
     * @param v value of integral type
     * @return reference to stream
     */
    template <typename T, typename I = std::decay_t<T>,
        typename = std::enable_if_t<std::is_integral<I>::value>>
    ScaleEncoderStream& operator<<(T&& v)
    {
        // encode bool
        if constexpr (std::is_same<I, bool>::value)
        {
            uint8_t byte = (v ? 1u : 0u);
            return putByte(byte);
        }
        // put byte
        if constexpr (sizeof(T) == 1u)
        {
// to avoid infinite recursion
#if __GNUC__ >= 10
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
            return putByte(static_cast<uint8_t>(v));
#if __GNUC__ >= 10
#pragma GCC diagnostic pop
#endif
        }
// encode any other integer
#if __GNUC__ >= 10
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        encodeInteger<I>(v, *this);
#if __GNUC__ >= 10
#pragma GCC diagnostic pop
#endif
        return *this;
    }

    /**
     * @brief scale-encodes CompactInteger value as compact integer
     * @param v value to encode
     * @return reference to stream
     */
    ScaleEncoderStream& operator<<(const CompactInteger& v);

    ScaleEncoderStream& operator<<(s256 const& v)
    {
        u256 unsignedValue = s2u(v);
        return *this << unsignedValue;
    }

    ScaleEncoderStream& operator<<(const u256& v);

protected:
    template <size_t I, class... Ts>
    void encodeElementOfTuple(const std::tuple<Ts...>& v)
    {
        *this << std::get<I>(v);
        if constexpr (sizeof...(Ts) > I + 1)
        {
            encodeElementOfTuple<I + 1>(v);
        }
    }

    template <uint8_t I, class... Ts>
    void tryEncodeAsOneOfVariant(const boost::variant<Ts...>& v)
    {
        using T = std::tuple_element_t<I, std::tuple<Ts...>>;
        if (v.type() == typeid(T))
        {
            *this << I << boost::get<T>(v);
            return;
        }
        if constexpr (sizeof...(Ts) > I + 1)
        {
            tryEncodeAsOneOfVariant<I + 1>(v);
        }
    }

    /**
     * @brief scale-encodes any collection
     * @tparam It iterator over collection of bytes
     * @param size size of the collection
     * @param begin iterator pointing to the begin of collection
     * @param end iterator pointing to the end of collection
     * @return reference to stream
     */
    template <class It>
    ScaleEncoderStream& encodeCollection(const CompactInteger& size, It&& begin, It&& end)
    {
        *this << size;
        for (auto&& it = begin; it != end; ++it)
        {
            *this << *it;
        }
        return *this;
    }

    /**
     * @brief puts a byte to buffer
     * @param v byte value
     * @return reference to stream
     */
    ScaleEncoderStream& putByte(uint8_t v)
    {
        m_stream.emplace_back(v);
        return *this;
    }

private:
    ScaleEncoderStream& encodeOptionalBool(const boost::optional<bool>& v);
    // std::deque<uint8_t> m_stream;
    std::deque<uint8_t> m_stream;
};
}  // namespace scale
}  // namespace codec
}  // namespace bcos