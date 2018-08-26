/**
* @CopyRight:
* This file is part of cpp-ethereum.
* cpp-ethereum is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* cpp-ethereum is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file vector_ref.h
 * @author Christian <c@ethdev.com>
 * @date 2015
 * A modifiable reference to an existing object or vector in memory.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#ifdef __INTEL_COMPILER
// will not be called for implicit or explicit conversions
#pragma warning(disable : 597)
#endif

namespace dev
{
/**
 * @brief : A modifiable reference to an existing object or vector in memory.
 * @tparam _T : type of the existing object or vecotr
 */
template <class _T>
class vector_ref
{
public:
    using value_type = _T;
    using element_type = _T;
    using mutable_value_type = typename std::conditional<std::is_const<_T>::value,
        typename std::remove_const<_T>::type, _T>::type;

    static_assert(std::is_pod<value_type>::value,
        "vector_ref can only be used with PODs due to its low-level treatment of data.");

    /**
     * @brief : constructors of vector_ref
     * @case1: creat an empty vector_ref
     * @case2: create a new vector_ref pointing to data part of a string
     * @case3: create a new vector_ref pointing to data part of a vector
     */
    vector_ref() : m_data(nullptr), m_count(0) {}

    /// create a new vector_ref pointint to _count elements starting at _data.
    vector_ref(_T* _data, size_t _count) : m_data(_data), m_count(_count) {}

    /// create a new vector_ref pointing to the data part of a string (given as pointer).
    vector_ref(
        typename std::conditional<std::is_const<_T>::value, std::string const*, std::string*>::type
            _data)
      : m_data(reinterpret_cast<_T*>(_data->data())), m_count(_data->size() / sizeof(_T))
    {}

    /// create a new vector_ref pointing to the data part of a vector (given as pointer).
    vector_ref(typename std::conditional<std::is_const<_T>::value,
        std::vector<typename std::remove_const<_T>::type> const*, std::vector<_T>*>::type _data)
      : m_data(_data->data()), m_count(_data->size())
    {}

    /// create a new vector_ref pointing to the data part of a string (given as reference).
    // for force convertions
    vector_ref(
        typename std::conditional<std::is_const<_T>::value, std::string const&, std::string&>::type
            _data)
      : m_data(reinterpret_cast<_T*>(_data.data())), m_count(_data.size() / sizeof(_T))
    {}

    /// judge whether the vector_ref pointing to an empty object or vector
    explicit operator bool() const { return m_data && m_count; }

    /// judge whether the vector_ref pointed content equals to inputted param _c
    bool contentsEqual(std::vector<mutable_value_type> const& _c) const
    {
        if (!m_data || m_count == 0)
            return _c.empty();
        else
            return _c.size() == m_count && !memcmp(_c.data(), m_data, m_count * sizeof(_T));
    }

    /// trans the vector_ref object to vector
    std::vector<mutable_value_type> toVector() const
    {
        return std::vector<mutable_value_type>(m_data, m_data + m_count);
    }

    /// trans to vector_ref to 'unsigned char const' vector
    std::vector<unsigned char> toBytes() const
    {
        return std::vector<unsigned char>(reinterpret_cast<unsigned char const*>(m_data),
            reinterpret_cast<unsigned char const*>(m_data) + m_count * sizeof(_T));
    }

    /// trans the vector to string
    std::string toString() const
    {
        return std::string((char const*)m_data, ((char const*)m_data) + m_count * sizeof(_T));
    }

    /// trans the vector_ref of _T type to the vector_ref of _T2 type
    template <class _T2>
    explicit operator vector_ref<_T2>() const
    {
        assert(m_count * sizeof(_T) / sizeof(_T2) * sizeof(_T2) / sizeof(_T) == m_count);
        return vector_ref<_T2>(reinterpret_cast<_T2*>(m_data), m_count * sizeof(_T) / sizeof(_T2));
    }

    /// trans vector_ref to constant vector_ref
    operator vector_ref<_T const>() const { return vector_ref<_T const>(m_data, m_count); }

    /// get data of the vector_ref
    _T* data() const { return m_data; }

    /// @returns the number of elements referenced (not necessarily number of bytes).
    size_t count() const { return m_count; }

    /// @returns the number of elements referenced (not necessarily number of bytes).
    size_t size() const { return m_count; }

    bool empty() const { return !m_count; }

    /// @returns a new vector_ref pointing at the next chunk of @a size() elements.
    vector_ref<_T> next() const
    {
        if (!m_data)
            return *this;
        else
            return vector_ref<_T>(m_data + m_count, m_count);
    }

    /// @returns a new vector_ref which is a shifted and shortened view of the original data.
    /// If this goes out of bounds in any way, returns an empty vector_ref.
    /// If @a _count is ~size_t(0), extends the view to the end of the data.
    vector_ref<_T> cropped(size_t _begin, size_t _count) const
    {
        if (m_data && _begin <= m_count && _count <= m_count && _begin + _count <= m_count)
            return vector_ref<_T>(
                m_data + _begin, _count == ~size_t(0) ? m_count - _begin : _count);
        else
            return vector_ref<_T>();
    }

    /// @returns a new vector_ref which is a shifted view of the original data
    // (not going beyond it).
    vector_ref<_T> cropped(size_t _begin) const
    {
        if (m_data && _begin <= m_count)
            return vector_ref<_T>(m_data + _begin, m_count - _begin);
        else
            return vector_ref<_T>();
    }

    /// retarget the vector_ref object to the new data
    void retarget(_T* _d, size_t _s)
    {
        m_data = _d;
        m_count = _s;
    }

    /// retarget the vector_ref object according to another vector_ref object
    void retarget(std::vector<_T> const& _t)
    {
        m_data = _t.data();
        m_count = _t.size();
    }

    /// judge whether the vector_ref object overlaps with _t
    template <class T>
    bool overlapsWith(vector_ref<T> _t) const
    {
        void const* f1 = data();
        void const* t1 = data() + size();
        void const* f2 = _t.data();
        void const* t2 = _t.data() + _t.size();
        return f1 < t2 && t1 > f2;
    }

    /// Copies the contents of this vector_ref to the contents of _t,
    /// up to the max size of _t.
    void copyTo(vector_ref<typename std::remove_const<_T>::type> _t) const
    {
        if (overlapsWith(_t))
            memmove(_t.data(), m_data, std::min(_t.size(), m_count) * sizeof(_T));
        else
            memcpy(_t.data(), m_data, std::min(_t.size(), m_count) * sizeof(_T));
    }

    /// Copies the contents of this vector_ref to the contents of _t,
    /// and zeros further trailing elements in _t.
    void populate(vector_ref<typename std::remove_const<_T>::type> _t) const
    {
        copyTo(_t);
        memset(_t.data() + m_count, 0, std::max(_t.size(), m_count) - m_count);
    }

    /// Securely overwrite the memory.
    /// @note adapted from OpenSSL's implementation.
    void cleanse()
    {
        static std::atomic<unsigned char> s_cleanseCounter{0u};
        uint8_t* p = (uint8_t*)begin();
        size_t const len = (uint8_t*)end() - p;
        size_t loop = len;
        size_t count = s_cleanseCounter;
        while (loop--)
        {
            *(p++) = (uint8_t)count;
            count += (17 + ((size_t)p & 0xf));
        }
        p = (uint8_t*)memchr((uint8_t*)begin(), (uint8_t)count, len);
        if (p)
            count += (63 + (size_t)p);
        s_cleanseCounter = (uint8_t)count;
        memset((uint8_t*)begin(), 0, len);
    }

    _T* begin() { return m_data; }
    _T* end() { return m_data + m_count; }
    _T const* begin() const { return m_data; }
    _T const* end() const { return m_data + m_count; }

    /// @return the ith element of m_data
    _T& operator[](size_t _i)
    {
        assert(m_data);
        assert(_i < m_count);
        return m_data[_i];
    }

    _T const& operator[](size_t _i) const
    {
        assert(m_data);
        assert(_i < m_count);
        return m_data[_i];
    }

    /// judge whether inputted vector_ref equals to the vector_ref(both content and size)
    bool operator==(vector_ref<_T> const& _cmp) const
    {
        return m_data == _cmp.m_data && m_count == _cmp.m_count;
    }

    bool operator!=(vector_ref<_T> const& _cmp) const { return !operator==(_cmp); }

    void reset()
    {
        m_data = nullptr;
        m_count = 0;
    }

private:
    _T* m_data;      // vector_ref pointted data
    size_t m_count;  // element size of the data
};
/// get reference of a object
template <class _T>
vector_ref<_T const> ref(_T const& _t)
{
    return vector_ref<_T const>(&_t, 1);
}
template <class _T>
vector_ref<_T> ref(_T& _t)
{
    return vector_ref<_T>(&_t, 1);
}
/// get reference of a vector
template <class _T>
vector_ref<_T const> ref(std::vector<_T> const& _t)
{
    return vector_ref<_T const>(&_t);
}
template <class _T>
vector_ref<_T> ref(std::vector<_T>& _t)
{
    return vector_ref<_T>(&_t);
}
}  // namespace dev
