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
 * @file WrapperBuffer.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once

#include "bcos-utilities/ObjectCounter.h"
#include "bcos-utilities/RefDataContainer.h"
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace bcos
{

class WrapperBuffer : bcos::ObjectCounter<WrapperBuffer>
{
public:
    using Ptr = std::shared_ptr<WrapperBuffer>;
    using ConstPtr = std::shared_ptr<const WrapperBuffer>;

    WrapperBuffer() = default;
    ~WrapperBuffer() = default;

    WrapperBuffer(const WrapperBuffer&) = delete;
    WrapperBuffer(WrapperBuffer&&) = delete;
    WrapperBuffer& operator=(const WrapperBuffer&) = delete;
    WrapperBuffer& operator=(WrapperBuffer&&) = delete;

    WrapperBuffer(bcos::bytes& _buffer) : m_end(_buffer.size()), m_buffer(std::move(_buffer)) {}
    WrapperBuffer(bcos::bytes&& _buffer) : m_end(_buffer.size()), m_buffer(std::move(_buffer)) {}

    bool canReadBuffer(std::size_t _readSize) const { return size() >= _readSize; }

    bool readBuffer(std::size_t _readSize, bcos::bytesConstRef& _data, bool _updateOffset = true)
    {
        if (!canReadBuffer(_readSize))
        {
            return false;
        }

        _data = {static_cast<bcos::byte*>(m_buffer.data()) + m_start, _readSize};
        if (_updateOffset)
        {
            incrStartOffset(_readSize);
        }

        return true;
    }

    std::size_t startOffset() const { return m_start; }
    std::size_t endOffset() const { return m_end; }
    std::size_t size() const { return m_end - m_start; }

    bcos::bytes& underlyingBuffer() { return m_buffer; }
    const bcos::bytes& underlyingBuffer() const { return m_buffer; }

    void reset()
    {
        m_buffer.clear();
        m_start = m_end = 0;
    }

    bcos::bytesConstRef asRefBuffer()
    {
        return {(bcos::byte*)m_buffer.data() + m_start, m_end - m_start};
    }

    bool incrStartOffset(std::size_t _step)
    {
        if (m_start + _step <= m_end)
        {
            m_start += _step;
            return true;
        }

        return false;
    }

    bool decrEndOffset(std::size_t _step)
    {
        if (m_end - _step < m_start)
        {
            return false;
        }

        m_end -= _step;
        return true;
    }

private:
    // the start offset of the actual data load
    std::size_t m_start{0};
    // the end offset of the actual data load
    std::size_t m_end{0};
    // binary buffer
    bcos::bytes m_buffer;
};

class WrapperBufferFactory
{
public:
    static WrapperBuffer::Ptr build() { return std::make_shared<WrapperBuffer>(); }

    static WrapperBuffer::Ptr build(bcos::bytes&& _buffer)
    {
        return std::make_shared<WrapperBuffer>(_buffer);
    }

    static WrapperBuffer::Ptr build(bcos::bytes& _buffer)
    {
        return std::make_shared<WrapperBuffer>(_buffer);
    }
};

}  // namespace bcos