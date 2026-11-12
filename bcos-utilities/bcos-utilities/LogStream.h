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

#include <array>
#include <atomic>
#include <charconv>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
#include <new>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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

// Read by every log statement's level check; written by admin_setLogLevel
// (IPC) and the SIGUSR2 reload. Atomic, as the two writers race with every
// logging thread. The hot read points (the BCOS_LOG macro and
// moduleLogEnabled) use relaxed loads — a plain LDR on ARM64; the remaining
// direct comparison sites are cold and go through the implicit seq_cst
// conversion, so no call site needs to change.
extern std::atomic<LogLevel> c_fileLogLevel;

constexpr auto operator<=>(LogLevel const& _lhs, auto const& _rhs)
    requires(std::same_as<decltype(_rhs), LogLevel> || std::integral<decltype(_rhs)>)
{
    return static_cast<int>(_lhs) <=> static_cast<int>(_rhs);
}

void setFileLogLevel(LogLevel const& _level);

/// Per-module (channel) log level. A module is the subsystem slot a log macro is wired to; the
/// badge text it prints is separate and may differ (PBFT_LOG prints [CONSENSUS][PBFT], GATEWAY
/// covers [Gateway], [P2PService] and [AMOP], EXECUTOR covers [DAG] and [PRECOMPILED]).
/// A table entry of -1 means "inherit the global c_fileLogLevel".
enum class LogModule : uint8_t
{
    PBFT,
    TXPOOL,
    SYNC,
    SCHEDULER,
    EXECUTOR,
    LEDGER,
    RPC,
    GATEWAY,
    FRONT,
    COUNT
};
constexpr int c_inheritGlobalLogLevel = -1;
extern std::array<std::atomic<int>, static_cast<size_t>(LogModule::COUNT)> c_moduleLogLevel;

inline bool moduleLogEnabled(LogModule _module, LogLevel _level)
{
    auto configured =
        c_moduleLogLevel[static_cast<size_t>(_module)].load(std::memory_order_relaxed);
    auto threshold = configured == c_inheritGlobalLogLevel ?
                         static_cast<int>(c_fileLogLevel.load(std::memory_order_relaxed)) :
                         configured;
    return static_cast<int>(_level) >= threshold;
}

std::string_view logModuleName(LogModule _module);
/// case-insensitive; nullopt when the name is not a channel
std::optional<LogModule> parseLogModule(std::string_view _name);
void setModuleLogLevel(LogModule _module, LogLevel _level);
void resetModuleLogLevel(LogModule _module);
/// returns false when _name is not a channel
bool setModuleLogLevel(std::string_view _name, LogLevel _level);
bool resetModuleLogLevel(std::string_view _name);
/// only the modules whose level is explicitly set (not inheriting)
std::vector<std::pair<std::string, LogLevel>> moduleLogLevels();

/// Everything the default line prefix needs, sampled on the producing thread at
/// the moment the log statement runs and rendered later on the feeding thread.
/// The timestamp is taken at the event, only its formatting is deferred, so the
/// timeline a log file shows is unaffected.
///
/// Trivially copyable and pointer-free on purpose: it crosses threads by value
/// and never needs lifetime management.
struct LineRecord
{
    LogLevel level = LogLevel::TRACE;
    std::int64_t secs = 0;       // wall-clock seconds since the epoch (local time zone applied at render)
    std::uint32_t micros = 0;    // 0..999999
    std::uint64_t threadId = 0;  // pthread_self() as an integer
    /// Thread name, NUL terminated. bcos::pthread_getThreadNameRef() is itself
    /// truncated to 15 characters by pthread_getname_np, so this fits.
    char threadName[16] = {};
};

namespace log
{
/// Samples the thread-invariant part of a record plus the current time.
LineRecord sampleLineRecord(LogLevel _level);
}  // namespace log

// Buffers one log record and commits it to the registered whole-line sinks on
// destruction. Constructed only when the level check in BCOS_LOG passes, so
// disabled log statements evaluate nothing.
//
// The record is assembled in an inline stack buffer (no heap allocation for
// lines up to InlineCapacity bytes, which covers virtually all log
// statements); longer lines spill to a doubling heap buffer. The buffered
// text is handed to the sinks exactly once on destruction.
//
// The insertion operators write straight into that buffer. Going through
// std::ostream per operand costs an order of magnitude more than the copy
// itself (sentry construction, a virtual sputn, and a locale facet lookup per
// integer), so the common operand kinds are formatted directly with
// std::to_chars. Operands of any other type fall back to an embedded
// std::ostream bridge that funnels into the same buffer, which keeps every
// existing operator<<(std::ostream&, T) overload working unchanged.
class LogStream
{
public:
    explicit LogStream(LogLevel _level) : m_record(log::sampleLineRecord(_level)) {}
    LogStream(LogStream const&) = delete;
    LogStream& operator=(LogStream const&) = delete;
    ~LogStream() noexcept;

    /// Sampled context of this statement, handed to the sink on commit.
    LineRecord const& record() const noexcept { return m_record; }

    // --- string-like operands: copied straight into the buffer ---
    LogStream& operator<<(char const* _value)
    {
        append(_value, std::strlen(_value));
        return *this;
    }
    LogStream& operator<<(char* _value) { return *this << static_cast<char const*>(_value); }
    LogStream& operator<<(std::string_view _value)
    {
        append(_value.data(), _value.size());
        return *this;
    }
    // std::string is intentionally not given its own overload: it converts to
    // string_view implicitly, and having both made `<<` ambiguous for
    // string_view arguments (each type converts to the other).

    // --- character operands: written verbatim, matching std::ostream ---
    // (signed/unsigned char are characters to std::ostream, not numbers)
    LogStream& operator<<(char _value)
    {
        appendChar(_value);
        return *this;
    }
    LogStream& operator<<(signed char _value)
    {
        appendChar(static_cast<char>(_value));
        return *this;
    }
    LogStream& operator<<(unsigned char _value)
    {
        appendChar(static_cast<char>(_value));
        return *this;
    }

    // --- bool renders as 1/0, the std::ostream default without boolalpha ---
    LogStream& operator<<(bool _value)
    {
        appendChar(_value ? '1' : '0');
        return *this;
    }

    // --- integrals: std::to_chars, no locale, no allocation ---
    template <std::integral T>
        requires(!std::same_as<T, char> && !std::same_as<T, signed char> &&
                 !std::same_as<T, unsigned char> && !std::same_as<T, bool>)
    LogStream& operator<<(T _value)
    {
        appendIntegral(_value);
        return *this;
    }

    // --- floating point: %g with precision 6, the std::ostream default ---
    LogStream& operator<<(float _value) { return appendFloating(_value); }
    LogStream& operator<<(double _value) { return appendFloating(_value); }
    LogStream& operator<<(long double _value) { return appendFloating(_value); }

    // --- pointers: 0x-prefixed lowercase hex, as std::ostream prints them ---
    LogStream& operator<<(void const* _value)
    {
        appendPointer(_value);
        return *this;
    }

    // --- stream manipulators ---
    LogStream& operator<<(std::ostream& (*_pf)(std::ostream&));
    LogStream& operator<<(std::ios& (*_pf)(std::ios&));
    LogStream& operator<<(std::ios_base& (*_pf)(std::ios_base&));

    // --- enums with no stream operator of their own: format through the
    // underlying type, which is exactly what the old pipeline produced via
    // integral promotion.
    //
    // An enum that DOES have its own operator<< must take the bridge instead:
    // an unconditional enum -> underlying conversion silently bypassed every
    // custom overload in this repository, so TransactionStatus::OutOfGas printed
    // as "12" instead of "OutOfGas" and BlockVersion printed as an integer
    // instead of "x.y.z".
    //
    // The distinguishing test is sound because a scoped enum (enum class) is not
    // implicitly convertible to its underlying type, so `os << v` is only valid
    // when a real overload exists. An unscoped enum promotes to int and is
    // therefore always valid; it takes the bridge too, which reproduces the old
    // promotion behaviour exactly.
    template <class T>
        requires std::is_enum_v<T> && (!requires(std::ostream& os, T const& v) { os << v; })
    LogStream& operator<<(T _value)
    {
        return *this << static_cast<std::underlying_type_t<T>>(_value);
    }

    // --- any other type that is streamable into std::ostream: bridge ---
    // Enums are not excluded: the ones that reach here have a real overload
    // (see above) and must be formatted by it.
    template <class T>
        requires(!std::is_array_v<std::remove_reference_t<T>> &&
                 !std::is_arithmetic_v<std::remove_cvref_t<T>> &&
                 !std::is_pointer_v<std::remove_cvref_t<T>> &&
                 !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
                 !std::is_convertible_v<T const&, std::string_view> &&
                 requires(std::ostream& os, T const& v) { os << v; })
    LogStream& operator<<(T const& _value);

    // Formatted record so far; valid until this LogStream is destroyed.
    std::string_view view() const noexcept
    {
        return {m_begin, static_cast<std::size_t>(m_cur - m_begin)};
    }

    // Raw append used by the std::ostream bridge.
    void appendRaw(char const* _data, std::size_t _size) { append(_data, _size); }

private:
    static constexpr std::size_t InlineCapacity = 512;

    // Forwards a std::ostream's writes into the owning LogStream's buffer.
    // Only reached for operands that need the generic operator<< path.
    class BridgeBuf : public std::streambuf
    {
    public:
        LogStream* m_target = nullptr;

    protected:
        std::streamsize xsputn(char const* _data, std::streamsize _count) override
        {
            if (m_target != nullptr)
            {
                m_target->appendRaw(_data, static_cast<std::size_t>(_count));
            }
            return _count;
        }
        int_type overflow(int_type _ch) override
        {
            if (traits_type::eq_int_type(_ch, traits_type::eof()))
            {
                return traits_type::eof();
            }
            if (m_target != nullptr)
            {
                m_target->appendChar(traits_type::to_char_type(_ch));
            }
            return _ch;
        }
    };

    static BridgeBuf& bridgeBuf()
    {
        thread_local BridgeBuf buf;
        return buf;
    }
    static std::ostream& bridgeStream()
    {
        thread_local std::ostream stream(&bridgeBuf());
        return stream;
    }

    // Entry point for every operand that needs the bridge.
    //
    // The bridge is a thread_local std::ostream shared by every statement on this
    // thread, so its state has to be established once per STATEMENT, not once per
    // operand:
    //
    //  - Once per statement, because a custom operator<< in the middle of a
    //    statement may deliberately change a flag for the operands that follow it
    //    (`stream << SetsHex{} << next` is the old pipeline's behaviour). Resetting
    //    per operand wiped that out and printed the following operands in the
    //    wrong base.
    //  - Once per statement, because leaving the state alone entirely let one
    //    statement's flags leak into every later statement on the thread.
    //
    // The error state is cleared before every use: the stream is never destroyed,
    // so a sticky failbit/badbit would turn every later insertion on this thread
    // into a silent no-op (ostream's sentry refuses to construct on a bad stream).
    std::ostream& bridgeForOperand()
    {
        auto& stream = bridgeStream();
        if (!m_bridgeReady)
        {
            // skipws is std::ostream's initial flag set; everything else is
            // default, except the base earlier operands of this statement chose.
            stream.flags(std::ios_base::skipws | baseFlag(m_base));
            stream.width(0);
            stream.precision(6);
            stream.fill(' ');
            m_bridgeReady = true;
        }
        stream.clear();
        return stream;
    }

    static std::ios_base::fmtflags baseFlag(int _base) noexcept
    {
        if (_base == 16)
        {
            return std::ios_base::hex;
        }
        if (_base == 8)
        {
            return std::ios_base::oct;
        }
        return std::ios_base::dec;
    }

    static int baseOf(std::ios_base::fmtflags _flags) noexcept
    {
        auto const base = _flags & std::ios_base::basefield;
        return base == std::ios_base::hex ? 16 : base == std::ios_base::oct ? 8 : 10;
    }

    /// After a bridged operand, fold the base it left behind back into m_base so
    /// the fast-path integers and floats that follow in the same statement agree
    /// with what the bridge would have printed.
    void syncBaseFromBridge()
    {
        m_base = baseOf(bridgeStream().flags());
    }

    /// Selects the integral base for both the fast path and (if it is already in
    /// use) the bridge, so the two cannot disagree within one statement.
    void setBase(int _base)
    {
        m_base = _base;
        if (m_bridgeReady)
        {
            auto flags = bridgeStream().flags() & ~std::ios_base::basefield;
            bridgeStream().flags(flags | baseFlag(_base));
        }
    }

    void append(char const* _data, std::size_t _size)
    {
        if (!ensure(_size))
        {
            return;
        }
        std::memcpy(m_cur, _data, _size);
        m_cur += _size;
    }
    void appendChar(char _value)
    {
        if (!ensure(1))
        {
            return;
        }
        *m_cur++ = _value;
    }

    template <std::integral T>
    void appendIntegral(T _value)
    {
        constexpr std::size_t c_maxDigits = 24;
        if (!ensure(c_maxDigits))
        {
            return;
        }
        std::to_chars_result result{};
        if (m_base != 10)
        {
            using UnsignedT = std::make_unsigned_t<T>;
            result = std::to_chars(
                m_cur, m_cur + c_maxDigits, static_cast<UnsignedT>(_value), m_base);
        }
        else
        {
            result = std::to_chars(m_cur, m_cur + c_maxDigits, _value);
        }
        m_cur = result.ptr;
    }

    template <class T>
    LogStream& appendFloating(T _value)
    {
        // std::ostream's default is %g at precision 6, C locale. The node calls
        // setlocale(LC_ALL, ""), so snprintf would follow the environment's
        // LC_NUMERIC and could print ',' — std::to_chars is locale-independent
        // and its precision overload is specified as printf-style %g.
        char buf[64];
        auto const result = std::to_chars(
            buf, buf + sizeof(buf), _value, std::chars_format::general, 6);
        if (result.ec == std::errc())
        {
            append(buf, static_cast<std::size_t>(result.ptr - buf));
        }
        return *this;
    }

    void appendPointer(void const* _value)
    {
        // std::ostream prints pointers as 0x + lowercase hex, C locale.
        char buf[24];
        buf[0] = '0';
        buf[1] = 'x';
        auto const result = std::to_chars(buf + 2, buf + sizeof(buf),
            reinterpret_cast<std::uintptr_t>(_value), 16);
        append(buf, static_cast<std::size_t>(result.ptr - buf));
    }

    // Makes room for _extra more bytes; false when the buffer is out of memory,
    // in which case the record stays truncated (same contract as before).
    bool ensure(std::size_t _extra)
    {
        if (static_cast<std::size_t>(m_end - m_cur) >= _extra)
        {
            return true;
        }
        return grow(_extra);
    }

    bool grow(std::size_t _extra)
    {
        auto const used = static_cast<std::size_t>(m_cur - m_begin);
        auto const capacity = static_cast<std::size_t>(m_end - m_begin);
        std::size_t newCapacity = capacity * 2;
        if (newCapacity < used + _extra)
        {
            newCapacity = (used + _extra) * 2;
        }
        auto* heap = new (std::nothrow) char[newCapacity];
        if (heap == nullptr)
        {
            return false;
        }
        std::memcpy(heap, m_begin, used);
        delete[] m_heap;
        m_heap = heap;
        m_begin = heap;
        m_cur = heap + used;
        m_end = heap + newCapacity;
        return true;
    }

    char m_inline[InlineCapacity];
    char* m_begin = m_inline;
    char* m_cur = m_inline;
    char* m_end = m_inline + InlineCapacity;
    char* m_heap = nullptr;
    int m_base = 10;
    /// True once this statement has initialised the thread_local bridge; see
    /// bridgeForOperand() for why the initialisation is per statement.
    bool m_bridgeReady = false;
    LineRecord m_record;
};

// --- manipulators -----------------------------------------------------------
// std::hex/std::dec/std::oct select the integral base; std::endl, std::ends and
// std::flush are the only ostream manipulators the log statements in this
// repository use. Anything else is applied to the std::ostream bridge so that
// it still behaves like the old pipeline, with the bridge's formatting state
// reset first (the bridge is thread_local and must not leak flags between
// statements).
inline LogStream& LogStream::operator<<(std::ostream& (*_pf)(std::ostream&))
{
    if (_pf == &std::endl<char, std::char_traits<char>>)
    {
        appendChar('\n');
        return *this;
    }
    if (_pf == &std::ends<char, std::char_traits<char>>)
    {
        appendChar('\0');
        return *this;
    }
    if (_pf == &std::flush<char, std::char_traits<char>>)
    {
        return *this;
    }
    auto& buf = bridgeBuf();
    LogStream* const previous = buf.m_target;
    buf.m_target = this;
    bridgeForOperand() << _pf;
    buf.m_target = previous;
    syncBaseFromBridge();
    return *this;
}

inline LogStream& LogStream::operator<<(std::ios& (*_pf)(std::ios&))
{
    auto& buf = bridgeBuf();
    LogStream* const previous = buf.m_target;
    buf.m_target = this;
    bridgeForOperand() << _pf;
    buf.m_target = previous;
    syncBaseFromBridge();
    return *this;
}

inline LogStream& LogStream::operator<<(std::ios_base& (*_pf)(std::ios_base&))
{
    // libc++ declares std::hex/dec/oct as ios_base manipulators; they select
    // the integral base for the operands that follow.
    if (_pf == &std::hex)
    {
        setBase(16);
        return *this;
    }
    if (_pf == &std::dec)
    {
        setBase(10);
        return *this;
    }
    if (_pf == &std::oct)
    {
        setBase(8);
        return *this;
    }
    // No log statement in this repository uses any other ios_base manipulator
    // (boolalpha, showbase, ...); forward them to the bridge so the behaviour
    // stays identical should one appear.
    auto& buf = bridgeBuf();
    LogStream* const previous = buf.m_target;
    buf.m_target = this;
    bridgeForOperand() << _pf;
    buf.m_target = previous;
    syncBaseFromBridge();
    return *this;
}

template <class T>
    requires(!std::is_array_v<std::remove_reference_t<T>> &&
             !std::is_arithmetic_v<std::remove_cvref_t<T>> &&
             !std::is_pointer_v<std::remove_cvref_t<T>> &&
             !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
             !std::is_convertible_v<T const&, std::string_view> &&
             requires(std::ostream& os, T const& v) { os << v; })
LogStream& LogStream::operator<<(T const& _value)
{
    // Save and restore the bridge target so a user operator<< that itself logs
    // cannot redirect the outer record.
    auto& buf = bridgeBuf();
    LogStream* const previous = buf.m_target;
    buf.m_target = this;
    bridgeForOperand() << _value;
    buf.m_target = previous;
    // A user operator may have switched the base (`<< SetsHex{} << 255`); fold it
    // back so the fast-path operand that follows prints in the same base.
    syncBaseFromBridge();
    return *this;
}

#define BCOS_LOG(level)                                                                \
    if (bcos::LogLevel::level >= bcos::c_fileLogLevel.load(std::memory_order_relaxed)) \
    bcos::LogStream(bcos::LogLevel::level)

#define BCOS_MODULE_LOG(MODULE, level)                                          \
    if (bcos::moduleLogEnabled(bcos::LogModule::MODULE, bcos::LogLevel::level)) \
    bcos::LogStream(bcos::LogLevel::level)
// for block number log
#define BLOCK_NUMBER(NUMBER) "[blk-" << (NUMBER) << "]"

}  // namespace bcos
