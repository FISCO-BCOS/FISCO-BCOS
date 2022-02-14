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
 * @brief scale decoder
 * @file ScaleDecoderStream.cpp
 */
#pragma once
#include "Common.h"
#include "FixedWidthIntegerCodec.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/FixedBytes.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <array>
#include <gsl/span>

namespace bcos
{
namespace codec
{
namespace scale
{
class ScaleDecoderStream
{
public:
    // special tag to differentiate decoding streams from others
    static constexpr auto is_decoder_stream = true;
    explicit ScaleDecoderStream(gsl::span<byte const> span);

    /**
     * @brief scale-decodes pair of values
     * @tparam F first value type
     * @tparam S second value type
     * @param p pair of values to decode
     * @return reference to stream
     */
    template <class F, class S>
    ScaleDecoderStream& operator>>(std::pair<F, S>& p)
    {
        static_assert(!std::is_reference_v<F> && !std::is_reference_v<S>);
        return *this >> const_cast<std::remove_const_t<F>&>(p.first)  // NOLINT
               >> const_cast<std::remove_const_t<S>&>(p.second);      // NOLINT
    }

    /**
     * @brief scale-decoding of tuple
     * @tparam T enumeration of tuples types
     * @param v reference to tuple
     * @return reference to stream
     */
    template <class... T>
    ScaleDecoderStream& operator>>(std::tuple<T...>& v)
    {
        if constexpr (sizeof...(T) > 0)
        {
            decodeElementOfTuple<0>(v);
        }
        return *this;
    }

    /**
     * @brief scale-decoding of variant
     * @tparam T enumeration of various types
     * @param v reference to variant
     * @return reference to stream
     */
    template <class... Ts>
    ScaleDecoderStream& operator>>(boost::variant<Ts...>& v)
    {
        // first byte means type index
        uint8_t type_index = 0u;
        *this >> type_index;  // decode type index

        // ensure that index is in [0, types_count)
        if (type_index >= sizeof...(Ts))
        {
            BOOST_THROW_EXCEPTION(
                ScaleDecodeException() << errinfo_comment("exception for WRONG_TYPE_INDEX"));
        }

        tryDecodeAsOneOfVariant<0>(v, type_index);
        return *this;
    }

    /**
     * @brief scale-decodes shared_ptr value
     * @tparam T value type
     * @param v value to decode
     * @return reference to stream
     */
    template <class T>
    ScaleDecoderStream& operator>>(std::shared_ptr<T>& v)
    {
        using mutableT = std::remove_const_t<T>;

        static_assert(std::is_default_constructible_v<mutableT>);

        v = std::make_shared<mutableT>();
        return *this >> const_cast<mutableT&>(*v);  // NOLINT
    }

    /**
     * @brief scale-decodes unique_ptr value
     * @tparam T value type
     * @param v value to decode
     * @return reference to stream
     */
    template <class T>
    ScaleDecoderStream& operator>>(std::unique_ptr<T>& v)
    {
        using mutableT = std::remove_const_t<T>;

        static_assert(std::is_default_constructible_v<mutableT>);

        v = std::make_unique<mutableT>();
        return *this >> const_cast<mutableT&>(*v);  // NOLINT
    }

    /**
     * @brief scale-encodes any integral type including bool
     * @tparam T integral type
     * @param v value of integral type
     * @return reference to stream
     */
    template <typename T, typename I = std::decay_t<T>,
        typename = std::enable_if_t<std::is_integral<I>::value>>
    ScaleDecoderStream& operator>>(T& v)
    {
        // check bool
        if constexpr (std::is_same<I, bool>::value)
        {
            v = decodeBool();
            return *this;
        }
        // check byte
        if constexpr (sizeof(T) == 1u)
        {
            v = nextByte();
            return *this;
        }
        // decode any other integer
        v = decodeInteger<I>(*this);
        return *this;
    }

    /**
     * @brief scale-decodes any optional value
     * @tparam T type of optional value
     * @param v optional value reference
     * @return reference to stream
     */
    template <class T>
    ScaleDecoderStream& operator>>(boost::optional<T>& v)
    {
        using mutableT = std::remove_const_t<T>;

        static_assert(std::is_default_constructible_v<mutableT>);

        // optional bool is special case of optional values
        // it is encoded as one byte instead of two
        // as described in specification
        if constexpr (std::is_same<mutableT, bool>::value)
        {
            v = decodeOptionalBool();
            return *this;
        }
        // detect if optional has value
        bool has_value = false;
        *this >> has_value;
        if (!has_value)
        {
            v.reset();
            return *this;
        }
        // decode value
        v.emplace();
        return *this >> const_cast<mutableT&>(*v);  // NOLINT
    }

    ScaleDecoderStream& operator>>(u256& v);
    /**
     * @brief scale-decodes compact integer value
     * @param v compact integer reference
     * @return
     */
    ScaleDecoderStream& operator>>(CompactInteger& v);
    ScaleDecoderStream& operator>>(s256& v)
    {
        u256 unsignedValue;
        *this >> unsignedValue;
        v = u2s(unsignedValue);
        return *this;
    }

    template <unsigned N>
    ScaleDecoderStream& operator>>(FixedBytes<N>& fixedData)
    {
        for (unsigned i = 0; i < N; ++i)
        {
            *this >> fixedData[i];
        }
        return *this;
    }
    /**
     * @brief decodes vector of items
     * @tparam T item type
     * @param v reference to vector
     * @return reference to stream
     */
    template <class T>
    ScaleDecoderStream& operator>>(std::vector<T>& v)
    {
        using mutableT = std::remove_const_t<T>;
        using size_type = typename std::list<T>::size_type;

        static_assert(std::is_default_constructible_v<mutableT>);

        CompactInteger size{0u};
        *this >> size;

        auto item_count = size.convert_to<size_type>();
        std::vector<mutableT> vec;
        try
        {
            vec.resize(item_count);
        }
        catch (const std::bad_alloc&)
        {
            BOOST_THROW_EXCEPTION(
                ScaleDecodeException()
                << errinfo_comment("exception for TOO_MANY_ITEMS: " + std::to_string(item_count)));
        }
        if constexpr (sizeof(T) == 1u)
        {
            vec.assign(m_currentIterator, m_currentIterator + item_count);
            m_currentIterator += item_count;
            m_currentIndex += item_count;
        }
        else
        {
            for (size_type i = 0u; i < item_count; ++i)
            {
                *this >> vec[i];
            }
        }
        v = std::move(vec);
        return *this;
    }

    /**
     * @brief decodes map of pairs
     * @tparam T item type
     * @tparam F item type
     * @param m reference to map
     * @return reference to stream
     */
    template <class T, class F>
    ScaleDecoderStream& operator>>(std::map<T, F>& m)
    {
        using mutableT = std::remove_const_t<T>;
        static_assert(std::is_default_constructible_v<mutableT>);
        using mutableF = std::remove_const_t<F>;
        static_assert(std::is_default_constructible_v<mutableF>);

        using size_type = typename std::map<T, F>::size_type;

        CompactInteger size{0u};
        *this >> size;

        auto item_count = size.convert_to<size_type>();
        std::map<mutableT, mutableF> map;
        for (size_type i = 0u; i < item_count; ++i)
        {
            std::pair<mutableT, mutableF> p;
            *this >> p;
            map.emplace(std::move(p));
        }
        m = std::move(map);
        return *this;
    }

    /**
     * @brief decodes collection of items
     * @tparam T item type
     * @param v reference to collection
     * @return reference to stream
     */
    template <class T>
    ScaleDecoderStream& operator>>(std::list<T>& v)
    {
        using mutableT = std::remove_const_t<T>;
        using size_type = typename std::list<T>::size_type;

        static_assert(std::is_default_constructible_v<mutableT>);

        CompactInteger size{0u};
        *this >> size;

        auto item_count = size.convert_to<size_type>();

        std::list<T> lst;
        try
        {
            lst.reserve(item_count);
        }
        catch (const std::bad_alloc&)
        {
            BOOST_THROW_EXCEPTION(ScaleDecodeException() << errinfo_comment(
                                      "exception for TOO_MANY_ITEMS" + std::to_string(item_count)));
        }

        for (size_type i = 0u; i < item_count; ++i)
        {
            lst.emplace_back();
            *this >> lst.back();
        }
        v = std::move(lst);
        return *this;
    }

    /**
     * @brief decodes array of items
     * @tparam T item type
     * @tparam size of the array
     * @param a reference to the array
     * @return reference to stream
     */
    template <class T, size_t size>
    ScaleDecoderStream& operator>>(std::array<T, size>& a)
    {
        using mutableT = std::remove_const_t<T>;
        for (size_t i = 0u; i < size; ++i)
        {
            *this >> const_cast<mutableT&>(a[i]);  // NOLINT
        }
        return *this;
    }

    /**
     * @brief decodes string from stream
     * @param v value to decode
     * @return reference to stream
     */
    ScaleDecoderStream& operator>>(std::string& v);

    /**
     * @brief hasMore Checks whether n more bytes are available
     * @param n Number of bytes to check
     * @return True if n more bytes are available and false otherwise
     */
    bool hasMore(uint64_t n) const;

    /**
     * @brief takes one byte from stream and
     * advances current byte iterator by one
     * @return current byte
     */
    uint8_t nextByte()
    {
        if (!hasMore(1))
        {
            BOOST_THROW_EXCEPTION(ScaleDecodeException()
                                  << errinfo_comment("nextByte exception for NOT_ENOUGH_DATA"));
        }
        ++m_currentIndex;
        return *m_currentIterator++;
    }
    using SizeType = gsl::span<const byte>::size_type;

    gsl::span<byte const> span() const { return m_span; }
    SizeType currentIndex() const { return m_currentIndex; }

private:
    bool decodeBool();
    /**
     * @brief special case of optional values as described in specification
     * @return boost::optional<bool> value
     */
    boost::optional<bool> decodeOptionalBool();

    template <size_t I, class... Ts>
    void decodeElementOfTuple(std::tuple<Ts...>& v)
    {
        using T = std::remove_const_t<std::tuple_element_t<I, std::tuple<Ts...>>>;
        *this >> const_cast<T&>(std::get<I>(v));  // NOLINT
        if constexpr (sizeof...(Ts) > I + 1)
        {
            decodeElementOfTuple<I + 1>(v);
        }
    }

    template <size_t I, class... Ts>
    void tryDecodeAsOneOfVariant(boost::variant<Ts...>& v, size_t i)
    {
        using T = std::remove_const_t<std::tuple_element_t<I, std::tuple<Ts...>>>;
        static_assert(std::is_default_constructible_v<T>);
        if (I == i)
        {
            T val;
            *this >> val;
            v = std::forward<T>(val);
            return;
        }
        if constexpr (sizeof...(Ts) > I + 1)
        {
            tryDecodeAsOneOfVariant<I + 1>(v, i);
        }
    }

private:
    gsl::span<byte const> m_span;
    gsl::span<byte const>::const_iterator m_currentIterator;
    SizeType m_currentIndex;
};
}  // namespace scale
}  // namespace codec
}  // namespace bcos