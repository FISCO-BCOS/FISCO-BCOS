/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file secure_vector.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * manage vector securely
 *
 */
#pragma once
#include "vector_ref.h"
#include <vector>

namespace bcos
{
template <class T>
class secure_vector
{
public:
    secure_vector() {}
    secure_vector(secure_vector<T> const& /*_c*/) = default;
    /**
     * @brief constructors of secure_vector
     * @case1: allocate vector with specified size
     * @case2: construct secure_vector from given vector
     * @case3: construct secure_vector from given vector_ref
     * @case4: construct secure_vector from given vector_ref referring to const object
     */

    /// allocate vector with specified size
    explicit secure_vector(size_t _size) : m_data(_size) {}

    /// construct secure_vector from given size of item
    explicit secure_vector(size_t _size, T _item) : m_data(_size, _item) {}

    /// construct secure_vector from given vector
    explicit secure_vector(std::vector<T> const& _c) : m_data(_c) {}

    /// construct secure_vector from given vector_ref
    explicit secure_vector(vector_ref<T> _c) : m_data(_c.data(), _c.data() + _c.size()) {}

    /// construct secure_vector from given vector_ref referring to const object
    explicit secure_vector(vector_ref<const T> _c) : m_data(_c.data(), _c.data() + _c.size()) {}
    ~secure_vector() { ref().cleanse(); }  // clean memory securely

    /// assign another secure_vector to this secure_vector securely
    secure_vector<T>& operator=(secure_vector<T> const& _c)
    {
        if (&_c == this)
            return *this;

        ref().cleanse();
        m_data = _c.m_data;
        return *this;
    }

    std::vector<T>& writable()
    {
        clear();
        return m_data;
    }

    std::vector<T> const& makeInsecure() const { return m_data; }

    void clear() { ref().cleanse(); }

    vector_ref<T> ref() { return vector_ref<T>(&m_data); }
    vector_ref<T const> ref() const { return vector_ref<T const>(&m_data); }

    size_t size() const { return m_data.size(); }
    bool empty() const { return m_data.empty(); }

    void swap(secure_vector<T>& io_other) { m_data.swap(io_other.m_data); }

private:
    std::vector<T> m_data;
};
}  // namespace bcos
