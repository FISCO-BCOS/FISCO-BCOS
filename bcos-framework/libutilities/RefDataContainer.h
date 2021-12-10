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
 *  @brief: manager the STL data without copy overhead
 *  @file RefDataContainer.h
 *  @date 2021-02-24
 */
#pragma once
#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace bcos
{
template <typename T>
class RefDataContainer
{
public:
    RefDataContainer() = default;
    RefDataContainer(T* _data, size_t _count) : m_dataPointer(_data), m_dataCount(_count) {}
    using RequiredStringPointerType =
        std::conditional<std::is_const<T>::value, std::string const*, std::string*>;
    using RequiredVecType = std::conditional<std::is_const<T>::value,
        std::vector<typename std::remove_const<T>::type> const*, std::vector<T>*>;
    using RequiredStringRefType =
        std::conditional<std::is_const<T>::value, std::string const&, std::string&>;

    RefDataContainer(typename RequiredStringPointerType::type _data)
      : m_dataPointer(reinterpret_cast<T*>(_data->data())), m_dataCount(_data->size() / sizeof(T))
    {}

    RefDataContainer(typename RequiredVecType::type _data)
      : m_dataPointer(reinterpret_cast<T*>(_data->data())), m_dataCount(_data->size())
    {}

    RefDataContainer(typename RequiredStringRefType::type _data)
      : m_dataPointer(reinterpret_cast<T*>(_data.data())), m_dataCount(_data.size() / sizeof(T))
    {}

    operator RefDataContainer<T const>() const
    {
        return RefDataContainer<T const>(m_dataPointer, m_dataCount);
    }

    explicit operator bool() { return m_dataPointer && m_dataCount; }

    T& operator[](size_t _index)
    {
        assert(_index < m_dataCount);
        return m_dataPointer[_index];
    }

    T const& operator[](size_t _index) const
    {
        assert(_index < m_dataCount);
        return m_dataPointer[_index];
    }

    bool operator==(RefDataContainer<T> _comparedContainer) const
    {
        return (data() == _comparedContainer.data()) && (size() == _comparedContainer.size());
    }

    bool operator!=(RefDataContainer<T> _comparedContainer) const
    {
        return !(operator==(_comparedContainer));
    }

    T* data() const { return m_dataPointer; }
    T* begin() const { return m_dataPointer; }
    T* end() const { return m_dataPointer + m_dataCount; }

    size_t count() const { return m_dataCount; }
    size_t size() const { return m_dataCount; }
    std::vector<unsigned char> toBytes() const
    {
        unsigned const char* dataPointer = reinterpret_cast<unsigned const char*>(m_dataPointer);
        return std::vector<unsigned char>(dataPointer, dataPointer + m_dataCount * sizeof(T));
    }

    std::string toString() const
    {
        return std::string(
            (char const*)m_dataPointer, ((char const*)m_dataPointer) + m_dataCount * sizeof(T));
    }

    bool empty() { return (m_dataCount == 0); }
    RefDataContainer<T> getCroppedData(size_t _startIndex, size_t _count) const
    {
        if (m_dataPointer && _startIndex <= m_dataCount && _count <= m_dataCount &&
            _startIndex + _count <= m_dataCount)
        {
            return RefDataContainer<T>(m_dataPointer + _startIndex, _count);
        }
        return RefDataContainer<T>();
    }

    /**
     * @brief Get the Cropped Data object
     *
     * @param _startIndex
     * @return RefDataContainer<T>
     */
    RefDataContainer<T> getCroppedData(size_t _startIndex) const
    {
        if (m_dataPointer && m_dataCount >= _startIndex)
        {
            return getCroppedData(_startIndex, (m_dataCount - _startIndex));
        }
        return RefDataContainer<T>();
    }

    bool dataOverlap(RefDataContainer<T> _dataContainer)
    {
        // data overlap
        if (begin() < _dataContainer.end() && _dataContainer.begin() < end())
        {
            return true;
        }
        return false;
    }

    // populate new RefDataContainer
    void populate(RefDataContainer<T> _dataContainer)
    {
        // data overlap
        if (dataOverlap(_dataContainer))
        {
            memmove((void*)_dataContainer.data(), (void*)data(),
                std::min(_dataContainer.count(), count()) * sizeof(T));
        }
        else
        {
            memcpy((void*)_dataContainer.data(), (void*)data(),
                std::min(_dataContainer.count(), count()) * sizeof(T));
        }
        // reset the remaining data to 0
        if (_dataContainer.count() > count())
        {
            memset((void*)(_dataContainer.data() + count() * sizeof(T)), 0,
                (_dataContainer.count() - count()) * sizeof(T));
        }
    }

    void reset()
    {
        m_dataPointer = nullptr;
        m_dataCount = 0;
    }

    void retarget(T* _newDataPointer, size_t _newSize)
    {
        m_dataPointer = _newDataPointer;
        m_dataCount = _newSize;
    }

    void cleanMemory()
    {
        static std::atomic<unsigned char> s_cleanCounter{0u};
        uint8_t* p = (uint8_t*)begin();
        size_t const len = (uint8_t*)end() - p;
        size_t loop = len;
        size_t count = s_cleanCounter;
        while (loop--)
        {
            *(p++) = (uint8_t)count;
            count += (17 + ((size_t)p & 0xf));
        }
        p = (uint8_t*)memchr((uint8_t*)begin(), (uint8_t)count, len);
        if (p)
            count += (63 + (size_t)p);
        s_cleanCounter = (uint8_t)count;
        memset((uint8_t*)begin(), 0, len);
    }

private:
    T* m_dataPointer = nullptr;
    size_t m_dataCount = 0;
};

template <typename T>
RefDataContainer<T> ref(T& _data)
{
    return RefDataContainer<T>(_data.data(), _data.size());
}

template <typename T>
RefDataContainer<T> ref(std::vector<T>& _data)
{
    return RefDataContainer<T>(_data.data(), _data.size());
}

template <typename T>
RefDataContainer<T const> ref(std::vector<T> const& _data)
{
    return RefDataContainer<T const>(_data.data(), _data.size());
}
}  // namespace bcos
