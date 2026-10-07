/**
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
 * @brief: lightweight log facade. BCOS_LOG and the LOG_* formatting macros
 *  expand to bcos::LogStream, so including this header does NOT pull in any
 *  boost/log headers; the actual logging backend lives in BoostLog.cpp.
 *  Code that needs the raw boost::log logger objects (FileLoggerHandler etc.)
 *  should include BoostLog.h instead.
 *
 * @file: LogStream.h
 */
#pragma once

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef ERROR
#undef ERROR
#endif

#ifdef TRACE
#undef TRACE
#endif

#ifdef INFO
#undef INFO
#endif

#ifdef WARNING
#undef WARNING
#endif

#ifdef FATAL
#undef FATAL
#endif

#include <compare>
#include <concepts>
#include <cstring>
#include <new>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

// BCOS log format
#ifndef LOG_BADGE
#define LOG_BADGE(_NAME) "[" << (_NAME) << "]"
#endif

#ifndef LOG_TYPE
#define LOG_TYPE(_TYPE) (_TYPE) << "|"
#endif

#ifndef LOG_DESC
#define LOG_DESC(_DESCRIPTION) (_DESCRIPTION)
#endif

#ifndef LOG_KV
#define LOG_KV(_K, _V) "," << (_K) << "=" << (_V)
#endif

namespace bcos
{
// Values must match boost::log::trivial::severity_level (checked by
// static_assert in BoostLog.cpp); kept boost-free so this header stays light.
enum LogLevel
{
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    FATAL = 5,
};

extern LogLevel c_fileLogLevel;
extern LogLevel c_statLogLevel;

constexpr auto operator<=>(LogLevel const& _lhs, auto const& _rhs)
    requires(std::same_as<decltype(_rhs), LogLevel> || std::integral<decltype(_rhs)>)
{
    return static_cast<int>(_lhs) <=> static_cast<int>(_rhs);
}

void setFileLogLevel(LogLevel const& _level);
void setStatLogLevel(LogLevel const& _level);

// Buffers one log record and commits it to the file logger on destruction.
// Constructed only when the level check in BCOS_LOG passes, so disabled log
// statements evaluate nothing.
//
// The record is assembled in an inline stack buffer (no heap allocation for
// lines up to InlineCapacity bytes, which covers virtually all log
// statements); longer lines spill to a doubling heap buffer. The buffered
// text is handed to the boost log record exactly once on destruction.
class LogStream
{
public:
    explicit LogStream(LogLevel _level) : m_level(_level) {}
    LogStream(LogStream const&) = delete;
    LogStream& operator=(LogStream const&) = delete;
    ~LogStream() noexcept;

    template <class T>
    LogStream& operator<<(T&& _value)
    {
        m_stream << std::forward<T>(_value);
        return *this;
    }
    // stream manipulators (std::endl, std::hex, ...)
    LogStream& operator<<(std::ostream& (*_pf)(std::ostream&))
    {
        _pf(m_stream);
        return *this;
    }
    LogStream& operator<<(std::ios& (*_pf)(std::ios&))
    {
        _pf(m_stream);
        return *this;
    }
    LogStream& operator<<(std::ios_base& (*_pf)(std::ios_base&))
    {
        _pf(m_stream);
        return *this;
    }

    // Formatted record so far; valid until this LogStream is destroyed.
    std::string_view view() const noexcept { return m_buffer.view(); }

private:
    static constexpr std::size_t InlineCapacity = 512;

    class Buffer : public std::streambuf
    {
    public:
        Buffer() { setp(m_inline, m_inline + InlineCapacity); }
        Buffer(Buffer const&) = delete;
        Buffer& operator=(Buffer const&) = delete;
        ~Buffer() override { delete[] m_heap; }

        std::string_view view() const noexcept
        {
            return {pbase(), static_cast<std::size_t>(pptr() - pbase())};
        }

    protected:
        int_type overflow(int_type _ch) override
        {
            if (traits_type::eq_int_type(_ch, traits_type::eof()) || !grow(1))
            {
                return traits_type::eof();
            }
            *pptr() = traits_type::to_char_type(_ch);
            pbump(1);
            return _ch;
        }
        std::streamsize xsputn(char const* _data, std::streamsize _count) override
        {
            if (_count > epptr() - pptr() && !grow(static_cast<std::size_t>(_count)))
            {
                return 0;
            }
            std::memcpy(pptr(), _data, static_cast<std::size_t>(_count));
            pbump(static_cast<int>(_count));
            return _count;
        }

    private:
        bool grow(std::size_t _extra)
        {
            auto const used = static_cast<std::size_t>(pptr() - pbase());
            auto const capacity = static_cast<std::size_t>(epptr() - pbase());
            std::size_t newCapacity = capacity * 2;
            if (newCapacity < used + _extra)
            {
                newCapacity = (used + _extra) * 2;
            }
            auto* heap = new (std::nothrow) char[newCapacity];
            if (heap == nullptr)
            {
                return false;  // out of memory: stream goes bad, record stays truncated
            }
            std::memcpy(heap, pbase(), used);
            delete[] m_heap;
            m_heap = heap;
            setp(heap, heap + newCapacity);
            pbump(static_cast<int>(used));
            return true;
        }

        char m_inline[InlineCapacity];
        char* m_heap = nullptr;
    };

    Buffer m_buffer;
    std::ostream m_stream{&m_buffer};
    LogLevel m_level;
};

#define BCOS_LOG(level)                                \
    if (bcos::LogLevel::level >= bcos::c_fileLogLevel) \
    bcos::LogStream(bcos::LogLevel::level)
// for block number log
#define BLOCK_NUMBER(NUMBER) "[blk-" << (NUMBER) << "]"

}  // namespace bcos
