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
 * @brief: define Log
 *
 * @file: Log.cpp
 * @author: yujiechen
 * @date 2021-02-24
 * @author: xingqiangbai
 * @date 2024-01-18
 * @brief: add file collector
 */
#include "BoostLog.h"
#include "BoostLogCollector.h"
#include "Common.h"
#include "GzTools.h"
#include "LineAsyncSink.h"
#include "LineLayout.h"
#include "Log.h"
#include <fmt/format.h>
#include <boost/date_time/time_facet.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/intrusive/link_mode.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>
#include <boost/log/attributes/time_traits.hpp>
#include <boost/log/core.hpp>
#include <boost/log/detail/singleton.hpp>
#include <boost/log/trivial.hpp>
#include <boost/make_shared.hpp>
#include <boost/spirit/home/qi/numeric/numeric_utils.hpp>
#include <boost/system/detail/error_category.hpp>
#include <boost/system/detail/error_code.hpp>
#ifdef _WIN32
// NOMINMAX: windows.h otherwise defines min/max as function-like macros,
// which leak ahead in unity builds and break unparenthesized
// std::numeric_limits<...>::max() in BoostLogInitializer.cpp.
#define NOMINMAX
#include <windows.h>
// wingdi.h (pulled in by windows.h) defines ERROR as a macro, which breaks
// LogLevel::ERROR below; LogStream.h's #undef cannot help because windows.h
// is included after it here (and leaks ahead in unity builds).
#undef ERROR
#else
#include <pthread.h>
#include <unistd.h>
#endif
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>
namespace bcos
{
std::string const FileLogger = "FileLogger";

// LogLevel is defined in LogStream.h without referencing boost; make sure the
// values stay in sync with boost::log::trivial::severity_level.
static_assert(static_cast<int>(LogLevel::TRACE) == boost::log::trivial::severity_level::trace);
static_assert(static_cast<int>(LogLevel::DEBUG) == boost::log::trivial::severity_level::debug);
static_assert(static_cast<int>(LogLevel::INFO) == boost::log::trivial::severity_level::info);
static_assert(static_cast<int>(LogLevel::WARNING) == boost::log::trivial::severity_level::warning);
static_assert(static_cast<int>(LogLevel::ERROR) == boost::log::trivial::severity_level::error);
static_assert(static_cast<int>(LogLevel::FATAL) == boost::log::trivial::severity_level::fatal);

std::atomic<LogLevel> c_fileLogLevel = LogLevel::TRACE;

namespace log
{
namespace
{
// Process-wide registry of whole-line sinks. std::atomic<std::shared_ptr> is
// not portable (libc++ rejects it), so the slots are plain shared_ptrs
// guarded by a shared_mutex: readers hold the shared lock across writeLine,
// and unregisterLineSink takes the unique lock, so it waits for in-flight
// producers before a sink can be destroyed. Register/unregister are rare
// (initLog/stopLogging), so the lock is never contended in practice.
constexpr std::size_t MaxLineSinks = 4;
struct LineSinkRegistry
{
    std::array<std::shared_ptr<LineSinkWriter>, MaxLineSinks> sinks;
    std::shared_mutex mutex;
    std::atomic<int> count{0};
};

// Leaked deliberately, the same lifetime contract as LineFeeder: when
// stopLogging() has not run, a log statement from another TU's static
// destructor can reach commitLine after this TU's statics are destroyed —
// with a leaked registry that degrades to a no-op instead of taking a
// shared lock on a destroyed std::shared_mutex.
LineSinkRegistry& registry()
{
    static auto* s_registry = new LineSinkRegistry();
    return *s_registry;
}

constexpr std::string_view c_severityNames[] = {
    "trace", "debug", "info", "warning", "error", "fatal"};

// "0x<tid>" exactly as the ThreadID attribute formats it; invariant per
// thread, so cached once.
std::string_view threadTidHex()
{
    thread_local std::string const tid = [] {
#ifdef _WIN32
        char buf[16];
        std::snprintf(
            buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(::GetCurrentThreadId()));
#else
        char buf[24];
        std::snprintf(buf, sizeof(buf), "0x%016llx",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pthread_self())));
#endif
        return std::string(buf);
    }();
    return tid;
}

// "<threadName>-0x<tid>" exactly as the ThreadName/ThreadID attributes format
// it; the value only changes when the thread is renamed, so cache it. Cache
// validity is tracked separately from the cached name: an empty name is a
// valid lookup result (unnamed threads on macOS, every thread on Windows)
// and must still build the "Unnamed-0x..." fragment on the first call.
std::string_view threadPart()
{
    thread_local bool initialized = false;
    thread_local std::string cachedName;
    thread_local std::string part;
    auto const& name = bcos::pthread_getThreadNameRef();
    if (!initialized || cachedName != name)
    {
        cachedName = name;
        part = name.empty() ? "Unnamed" : name;
        part += '-';
        part += threadTidHex();
        initialized = true;
    }
    return part;
}

// The wall clock as a broken-down local time plus the microsecond fraction.
// localtime_r takes glibc's internal timezone lock, so the broken-down time
// is cached per thread and recomputed only when the second ticks over.
struct BrokenDownTime
{
    std::tm tm{};
    long fracMicros = 0;  // 0..999999
};
BrokenDownTime brokenDownNow()
{
    auto const micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    std::time_t const secs = static_cast<std::time_t>(micros.count() / 1000000);
    thread_local std::time_t cachedSecs = 0;
    thread_local std::tm tm{};
    if (secs != cachedSecs)
    {
#ifdef _WIN32
        localtime_s(&tm, &secs);
#else
        localtime_r(&secs, &tm);
#endif
        cachedSecs = secs;
    }
    return {tm, static_cast<long>(micros.count() % 1000000)};
}

// Zero-padded decimal append; no allocation, no locale.
void appendUnsigned(std::string& _out, std::uint64_t _value, unsigned _minWidth = 1)
{
    char tmp[20];
    char* const end = tmp + sizeof(tmp);
    char* p = end;
    unsigned digits = 0;
    do
    {
        *--p = static_cast<char>('0' + (_value % 10));
        _value /= 10;
        ++digits;
    } while (_value != 0);
    while (digits < _minWidth)
    {
        _out += '0';
        ++digits;
    }
    _out.append(p, static_cast<std::size_t>(end - p));
}
}  // namespace

// "severity|YYYY-MM-DD HH:MM:SS.ffffff|thread|", byte-identical to the
// release-3.18.0 default formatter expression (attr<severity_level>
// ("Severity") << "|" << format_date_time<ptime>("TimeStamp",
// "%Y-%m-%d %H:%M:%S.%f") << "|" << attr<std::string>("ThreadName") << "-"
// << attr<current_thread_id>("ThreadID") << "|" << smessage), which this
// pipeline reimplements without boost::log.
void appendLinePrefix(std::string& _out, LogLevel _level)
{
    _out.append(c_severityNames[static_cast<int>(_level)]);
    _out += '|';
    auto const time = brokenDownNow();
    char buf[40];
    auto const formatted = fmt::format_to_n(buf, sizeof(buf),
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06}", time.tm.tm_year + 1900, time.tm.tm_mon + 1,
        time.tm.tm_mday, time.tm.tm_hour, time.tm.tm_min, time.tm.tm_sec, time.fracMicros);
    _out.append(buf, formatted.size);
    _out += '|';
    _out.append(threadPart());
    _out += '|';
}

namespace
{
// The installed custom layout; write-once per initLog call, never freed, so
// the hot path reads it with a single atomic load and no lifetime juggling.
std::atomic<LineLayout const*> s_lineLayout{nullptr};
// Every layout retired from s_lineLayout. The objects are never freed —
// producers on other threads may still be mid-render with them — and keeping
// them reachable also keeps them off the leak-sanitizer report. Leaked
// deliberately, the same lifetime contract as the registry and LineFeeder.
struct RetiredLayouts
{
    std::mutex mutex;
    std::vector<LineLayout const*> layouts;
};
RetiredLayouts& retiredLayouts()
{
    static auto* s_retired = new RetiredLayouts();
    return *s_retired;
}
// Global monotonically increasing id rendered by %LineID%, matching the
// boost LineID attribute semantics (first rendered id is 1). Only layouts
// containing %LineID% pay the increment.
std::atomic<std::uint64_t> s_lineID{0};

void appendTimeStamp(
    std::string& _out, std::vector<LineLayout::TimePart> const& _parts, BrokenDownTime _time)
{
    for (auto const& part : _parts)
    {
        switch (part.kind)
        {
        case LineLayout::TimePart::Kind::Literal:
            _out.append(part.literal);
            break;
        case LineLayout::TimePart::Kind::Year4:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.tm.tm_year) + 1900, 4);
            break;
        case LineLayout::TimePart::Kind::Year2:
            appendUnsigned(_out, (static_cast<std::uint64_t>(_time.tm.tm_year) + 1900) % 100, 2);
            break;
        case LineLayout::TimePart::Kind::Month:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.tm.tm_mon) + 1, 2);
            break;
        case LineLayout::TimePart::Kind::Day:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.tm.tm_mday), 2);
            break;
        case LineLayout::TimePart::Kind::Hour:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.tm.tm_hour), 2);
            break;
        case LineLayout::TimePart::Kind::Minute:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.tm.tm_min), 2);
            break;
        case LineLayout::TimePart::Kind::Second:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.tm.tm_sec), 2);
            break;
        case LineLayout::TimePart::Kind::Frac6:
            appendUnsigned(_out, static_cast<std::uint64_t>(_time.fracMicros), 6);
            break;
        }
    }
}

// Compiles a boost date_time format string (the flags the record-based
// pipeline accepted through boost's time_facet) into TimeParts.
bool compileTimeFormat(
    std::string_view _fmt, std::vector<LineLayout::TimePart>& _parts, std::string& _error)
{
    std::string literal;
    auto flushLiteral = [&]() {
        if (!literal.empty())
        {
            LineLayout::TimePart part;
            part.literal = std::move(literal);
            literal.clear();
            _parts.push_back(std::move(part));
        }
    };
    for (std::size_t i = 0; i < _fmt.size(); ++i)
    {
        if (_fmt[i] != '%')
        {
            literal += _fmt[i];
            continue;
        }
        if (i + 1 >= _fmt.size())
        {
            _error = "dangling '%' at end of TimeStamp format";
            return false;
        }
        LineLayout::TimePart part;
        switch (_fmt[i + 1])
        {
        case '%':
            literal += '%';
            ++i;
            continue;
        case 'Y':
            part.kind = LineLayout::TimePart::Kind::Year4;
            break;
        case 'y':
            part.kind = LineLayout::TimePart::Kind::Year2;
            break;
        case 'm':
            part.kind = LineLayout::TimePart::Kind::Month;
            break;
        case 'd':
            part.kind = LineLayout::TimePart::Kind::Day;
            break;
        case 'H':
            part.kind = LineLayout::TimePart::Kind::Hour;
            break;
        case 'M':
            part.kind = LineLayout::TimePart::Kind::Minute;
            break;
        case 'S':
            part.kind = LineLayout::TimePart::Kind::Second;
            break;
        case 'f':
            part.kind = LineLayout::TimePart::Kind::Frac6;
            break;
        default:
            _error = "unsupported date_time flag '%";
            _error += _fmt[i + 1];
            _error += "' in TimeStamp format";
            return false;
        }
        flushLiteral();
        _parts.push_back(std::move(part));
        ++i;
    }
    flushLiteral();
    return true;
}

std::string_view trimSpaces(std::string_view _s)
{
    while (!_s.empty() && (_s.front() == ' ' || _s.front() == '\t'))
    {
        _s.remove_prefix(1);
    }
    while (!_s.empty() && (_s.back() == ' ' || _s.back() == '\t'))
    {
        _s.remove_suffix(1);
    }
    return _s;
}
}  // namespace

std::unique_ptr<LineLayout> parseLineLayout(std::string_view _format, std::string& _error)
{
    auto layout = std::make_unique<LineLayout>();
    {
        std::string pid;
#ifdef _WIN32
        appendUnsigned(pid, static_cast<std::uint64_t>(::GetCurrentProcessId()));
#else
        appendUnsigned(pid, static_cast<std::uint64_t>(::getpid()));
#endif
        layout->processID = std::move(pid);
    }

    std::string literal;
    auto flushLiteral = [&]() {
        if (!literal.empty())
        {
            LineLayout::Segment segment;
            segment.literal = std::move(literal);
            literal.clear();
            layout->segments.push_back(std::move(segment));
        }
    };

    std::size_t pos = 0;
    while (pos < _format.size())
    {
        if (_format[pos] != '%')
        {
            literal += _format[pos++];
            continue;
        }
        if (pos + 1 < _format.size() && _format[pos + 1] == '%')
        {
            literal += '%';
            pos += 2;
            continue;
        }
        // Placeholder: %Name% or %Name(args)%; the closing '%' is the first
        // one outside any quoted region (the TimeStamp format string itself
        // contains '%').
        std::size_t const bodyBegin = pos + 1;
        std::size_t i = bodyBegin;
        std::size_t close = std::string_view::npos;
        while (i < _format.size())
        {
            char const ch = _format[i];
            if (ch == '"')
            {
                ++i;
                while (i < _format.size() && _format[i] != '"')
                {
                    if (_format[i] == '\\' && i + 1 < _format.size())
                    {
                        ++i;
                    }
                    ++i;
                }
                if (i >= _format.size())
                {
                    _error = "unterminated quoted string";
                    return nullptr;
                }
                ++i;
                continue;
            }
            if (ch == '%')
            {
                close = i;
                break;
            }
            ++i;
        }
        if (close == std::string_view::npos)
        {
            _error = "unterminated '%' placeholder";
            return nullptr;
        }

        std::string_view const body = _format.substr(bodyBegin, close - bodyBegin);
        std::string_view name = body;
        std::string_view args;
        bool const hasArgs = body.find('(') != std::string_view::npos;
        if (hasArgs)
        {
            auto const paren = body.find('(');
            if (body.back() != ')')
            {
                _error = "malformed arguments in '%" + std::string(body) + "%'";
                return nullptr;
            }
            name = body.substr(0, paren);
            args = body.substr(paren + 1, body.size() - paren - 2);
        }
        name = trimSpaces(name);

        LineLayout::Segment segment;
        if (name == "Severity")
        {
            segment.field = LineLayout::Field::Severity;
        }
        else if (name == "TimeStamp")
        {
            segment.field = LineLayout::Field::TimeStamp;
        }
        else if (name == "ThreadName")
        {
            segment.field = LineLayout::Field::ThreadName;
        }
        else if (name == "ThreadID")
        {
            segment.field = LineLayout::Field::ThreadID;
        }
        else if (name == "LineID")
        {
            segment.field = LineLayout::Field::LineID;
        }
        else if (name == "ProcessID")
        {
            segment.field = LineLayout::Field::ProcessID;
        }
        else if (name == "Message")
        {
            segment.field = LineLayout::Field::Message;
        }
        else
        {
            _error = "unknown attribute '" + std::string(name) + "'";
            return nullptr;
        }

        if (hasArgs)
        {
            // Only %TimeStamp(format="...")% takes arguments.
            if (segment.field != LineLayout::Field::TimeStamp)
            {
                _error = "arguments are only supported on %TimeStamp(...)%";
                return nullptr;
            }
            args = trimSpaces(args);
            constexpr std::string_view formatKey = "format";
            if (!args.starts_with(formatKey))
            {
                _error = "unsupported argument in '%" + std::string(body) +
                         "%', expected format=\"...\"";
                return nullptr;
            }
            args = trimSpaces(args.substr(formatKey.size()));
            if (args.empty() || args.front() != '=')
            {
                _error = "expected '=' in '%" + std::string(body) + "%'";
                return nullptr;
            }
            args = trimSpaces(args.substr(1));
            if (args.size() < 2 || args.front() != '"' || args.back() != '"')
            {
                _error = "expected quoted string in '%" + std::string(body) + "%'";
                return nullptr;
            }
            std::string timeFormat;
            for (std::size_t k = 1; k + 1 < args.size(); ++k)
            {
                // \' \" \\ escapes, as in the boost formatter grammar
                if (args[k] == '\\' && k + 2 < args.size())
                {
                    ++k;
                }
                timeFormat += args[k];
            }
            if (!compileTimeFormat(timeFormat, segment.timeParts, _error))
            {
                return nullptr;
            }
        }
        else if (segment.field == LineLayout::Field::TimeStamp)
        {
            // boost renders a bare %TimeStamp% via to_simple_string, which at
            // microsecond resolution is exactly this format.
            if (!compileTimeFormat("%Y-%m-%d %H:%M:%S.%f", segment.timeParts, _error))
            {
                return nullptr;
            }
        }

        if (segment.field == LineLayout::Field::TimeStamp)
        {
            layout->usesTimeStamp = true;
        }
        else if (segment.field == LineLayout::Field::LineID)
        {
            layout->usesLineID = true;
        }
        flushLiteral();
        layout->segments.push_back(std::move(segment));
        pos = close + 1;
    }
    flushLiteral();
    return layout;
}

void setLineLayout(std::unique_ptr<LineLayout> _layout)
{
    // Leaked deliberately: producers on other threads may be mid-render with
    // the previous layout, and there is no safe reclamation point short of
    // per-line synchronization. The retired one stays reachable so it never
    // shows up on the leak-sanitizer report.
    LineLayout const* previous =
        s_lineLayout.exchange(_layout.release(), std::memory_order_acq_rel);
    if (previous != nullptr)
    {
        auto& retired = retiredLayouts();
        std::lock_guard lock(retired.mutex);
        retired.layouts.push_back(previous);
    }
}

LineLayout const* lineLayout() noexcept
{
    return s_lineLayout.load(std::memory_order_acquire);
}

void appendLayoutLine(
    std::string& _out, LineLayout const& _layout, LogLevel _level, std::string_view _message)
{
    // The clock is read only when the layout actually prints a timestamp.
    auto const time = _layout.usesTimeStamp ? brokenDownNow() : BrokenDownTime{};
    for (auto const& segment : _layout.segments)
    {
        switch (segment.field)
        {
        case LineLayout::Field::Literal:
            _out.append(segment.literal);
            break;
        case LineLayout::Field::Severity:
            _out.append(c_severityNames[static_cast<int>(_level)]);
            break;
        case LineLayout::Field::TimeStamp:
            appendTimeStamp(_out, segment.timeParts, time);
            break;
        case LineLayout::Field::ThreadName:
            _out.append(bcos::pthread_getThreadNameRef());
            break;
        case LineLayout::Field::ThreadID:
            _out.append(threadTidHex());
            break;
        case LineLayout::Field::LineID:
            appendUnsigned(_out, s_lineID.fetch_add(1, std::memory_order_relaxed) + 1);
            break;
        case LineLayout::Field::ProcessID:
            _out.append(_layout.processID);
            break;
        case LineLayout::Field::Message:
            _out.append(_message);
            break;
        }
    }
}

std::string formatLogLine(LogLevel _level, std::string_view _message)
{
    std::string line;
    if (auto const* layout = lineLayout(); layout != nullptr)
    {
        appendLayoutLine(line, *layout, _level, _message);
    }
    else
    {
        appendLinePrefix(line, _level);
        line.append(_message);
    }
    return line;
}

void registerLineSink(std::shared_ptr<LineSinkWriter> _sink)
{
    auto& reg = registry();
    std::unique_lock lock(reg.mutex);
    for (auto& slot : reg.sinks)
    {
        if (!slot)
        {
            slot = std::move(_sink);
            reg.count.fetch_add(1, std::memory_order_release);
            return;
        }
    }
    // All slots taken: more than MaxLineSinks sinks registered. Not reachable
    // with today's single-initializer deployments; say so instead of dropping
    // the sink silently.
    std::fputs("registerLineSink: registry full, sink not registered\n", stderr);
}

void unregisterLineSink(LineSinkWriter const* _sink)
{
    auto& reg = registry();
    std::unique_lock lock(reg.mutex);
    for (auto& slot : reg.sinks)
    {
        if (slot.get() == _sink)
        {
            slot.reset();
            reg.count.fetch_sub(1, std::memory_order_release);
            return;
        }
    }
}

bool hasLineSinks() noexcept
{
    return registry().count.load(std::memory_order_acquire) > 0;
}

std::uint64_t droppedLogLines() noexcept
{
    auto& reg = registry();
    std::shared_lock lock(reg.mutex);
    std::uint64_t total = 0;
    for (auto const& sink : reg.sinks)
    {
        if (sink)
        {
            total += sink->droppedTotal();
        }
    }
    return total;
}

// Fans the line out to every registered whole-line sink. The shared lock is
// held across writeLine: normally that is only the sink's queue critical
// section, so producers never block a reader on backend I/O. The FATAL path
// is the exception: it can hold the shared lock for up to
// 2 x FatalDrainTimeout (bounded drain + bounded backend acquisition), and a
// throwing backend makes it return without aborting — so
// register/unregister can stall up to two seconds against an in-flight
// FATAL. register/unregister are rare by construction.
void commitLine(LogLevel _level, std::string_view _message)
{
    thread_local std::string prefix;
    prefix.clear();
    if (auto const* layout = lineLayout(); layout != nullptr)
    {
        // Custom log.format: the layout places the message freely, so the
        // line is assembled whole and handed over as prefix + empty message.
        appendLayoutLine(prefix, *layout, _level, _message);
        _message = {};
    }
    else
    {
        appendLinePrefix(prefix, _level);
    }
    auto& reg = registry();
    std::shared_lock lock(reg.mutex);
    for (auto const& sink : reg.sinks)
    {
        if (sink)
        {
            sink->writeLine(_level, prefix, _message);
        }
    }
}
}  // namespace log

void setFileLogLevel(LogLevel const& _level)
{
    c_fileLogLevel.store(_level, std::memory_order_relaxed);
}

LogStream::~LogStream() noexcept
{
    try
    {
        // The line is committed to every registered whole-line sink; when
        // none is registered (before initLog / after stopLogging) it is
        // dropped, same as logging with no sinks attached used to behave.
        // The count check avoids formatting the prefix when logging is down.
        if (log::hasLineSinks())
        {
            log::commitLine(m_level, view());
        }
    }
    catch (...)
    {}
}

namespace
{
constexpr std::array<std::string_view, static_cast<size_t>(LogModule::COUNT)> c_logModuleNames = {
    "PBFT", "TXPOOL", "SYNC", "SCHEDULER", "EXECUTOR", "LEDGER", "RPC", "GATEWAY", "FRONT"};

}  // namespace

static_assert(static_cast<size_t>(LogModule::COUNT) == 9, "update c_moduleLogLevel initializer");
std::array<std::atomic<int>, static_cast<size_t>(LogModule::COUNT)> c_moduleLogLevel = {
    {c_inheritGlobalLogLevel, c_inheritGlobalLogLevel, c_inheritGlobalLogLevel,
        c_inheritGlobalLogLevel, c_inheritGlobalLogLevel, c_inheritGlobalLogLevel,
        c_inheritGlobalLogLevel, c_inheritGlobalLogLevel, c_inheritGlobalLogLevel}};

std::string_view logModuleName(LogModule _module)
{
    return c_logModuleNames[static_cast<size_t>(_module)];
}

std::optional<LogModule> parseLogModule(std::string_view _name)
{
    for (size_t i = 0; i < c_logModuleNames.size(); ++i)
    {
        auto candidate = c_logModuleNames[i];
        if (candidate.size() != _name.size())
        {
            continue;
        }
        bool equal = true;
        for (size_t j = 0; j < candidate.size(); ++j)
        {
            if (std::toupper(static_cast<unsigned char>(_name[j])) != candidate[j])
            {
                equal = false;
                break;
            }
        }
        if (equal)
        {
            return static_cast<LogModule>(i);
        }
    }
    return std::nullopt;
}

void setModuleLogLevel(LogModule _module, LogLevel _level)
{
    c_moduleLogLevel[static_cast<size_t>(_module)].store(
        static_cast<int>(_level), std::memory_order_relaxed);
}

void resetModuleLogLevel(LogModule _module)
{
    c_moduleLogLevel[static_cast<size_t>(_module)].store(
        c_inheritGlobalLogLevel, std::memory_order_relaxed);
}

bool setModuleLogLevel(std::string_view _name, LogLevel _level)
{
    auto module = parseLogModule(_name);
    if (!module)
    {
        return false;
    }
    setModuleLogLevel(*module, _level);
    return true;
}

bool resetModuleLogLevel(std::string_view _name)
{
    auto module = parseLogModule(_name);
    if (!module)
    {
        return false;
    }
    resetModuleLogLevel(*module);
    return true;
}

std::vector<std::pair<std::string, LogLevel>> moduleLogLevels()
{
    std::vector<std::pair<std::string, LogLevel>> result;
    for (size_t i = 0; i < c_moduleLogLevel.size(); ++i)
    {
        auto level = c_moduleLogLevel[i].load(std::memory_order_relaxed);
        if (level == c_inheritGlobalLogLevel)
        {
            continue;
        }
        result.emplace_back(std::string(c_logModuleNames[i]), static_cast<LogLevel>(level));
    }
    return result;
}

namespace
{  // copy form
using path_string_type = boost::filesystem::path::string_type;
using path_char_type = path_string_type::value_type;
using filesystem_error = boost::system::system_error;
using path_string_type = boost::filesystem::path::string_type;
using path_char_type = path_string_type::value_type;

//! An auxiliary traits that contain various constants and functions regarding string and character
//! operations
template <typename CharT>
struct file_char_traits;

template <>
struct file_char_traits<char>
{
    using char_type = char;

    static const char_type percent = '%';
    static const char_type number_placeholder = 'N';
    static const char_type day_placeholder = 'd';
    static const char_type month_placeholder = 'm';
    static const char_type year_placeholder = 'y';
    static const char_type full_year_placeholder = 'Y';
    static const char_type frac_sec_placeholder = 'f';
    static const char_type seconds_placeholder = 'S';
    static const char_type minutes_placeholder = 'M';
    static const char_type hours_placeholder = 'H';
    static const char_type space = ' ';
    static const char_type plus = '+';
    static const char_type minus = '-';
    static const char_type zero = '0';
    static const char_type dot = '.';
    static const char_type newline = '\n';

    static bool is_digit(char c)
    {
        using namespace std;
        return (isdigit(c) != 0);
    }
    static std::string default_file_name_pattern() { return "%5N.log"; }
};

#ifndef BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE
const file_char_traits<char>::char_type file_char_traits<char>::percent;
const file_char_traits<char>::char_type file_char_traits<char>::number_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::day_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::month_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::year_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::full_year_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::frac_sec_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::seconds_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::minutes_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::hours_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::space;
const file_char_traits<char>::char_type file_char_traits<char>::plus;
const file_char_traits<char>::char_type file_char_traits<char>::minus;
const file_char_traits<char>::char_type file_char_traits<char>::zero;
const file_char_traits<char>::char_type file_char_traits<char>::dot;
const file_char_traits<char>::char_type file_char_traits<char>::newline;
#endif  // BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE

template <>
struct file_char_traits<wchar_t>
{
    using char_type = wchar_t;

    static const char_type percent = L'%';
    static const char_type number_placeholder = L'N';
    static const char_type day_placeholder = L'd';
    static const char_type month_placeholder = L'm';
    static const char_type year_placeholder = L'y';
    static const char_type full_year_placeholder = L'Y';
    static const char_type frac_sec_placeholder = L'f';
    static const char_type seconds_placeholder = L'S';
    static const char_type minutes_placeholder = L'M';
    static const char_type hours_placeholder = L'H';
    static const char_type space = L' ';
    static const char_type plus = L'+';
    static const char_type minus = L'-';
    static const char_type zero = L'0';
    static const char_type dot = L'.';
    static const char_type newline = L'\n';

    static bool is_digit(wchar_t c)
    {
        using namespace std;
        return (iswdigit(c) != 0);
    }
    static std::wstring default_file_name_pattern() { return L"%5N.log"; }
};

#ifndef BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::percent;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::number_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::day_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::month_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::year_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::full_year_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::frac_sec_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::seconds_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::minutes_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::hours_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::space;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::plus;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::minus;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::zero;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::dot;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::newline;
#endif  // BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE

//! The functor formats the file counter into the file name
class file_counter_formatter
{
public:
    using result_type = path_string_type;

private:
    //! The position in the pattern where the file counter placeholder is
    path_string_type::size_type m_FileCounterPosition;
    std::streamsize m_Width;                                    //! File counter width
    mutable std::basic_ostringstream<path_char_type> m_Stream;  //! File counter formatting stream

public:
    //! Initializing constructor
    file_counter_formatter(path_string_type::size_type pos, unsigned int width)
      : m_FileCounterPosition(pos), m_Width(width)
    {
        typedef file_char_traits<path_char_type> traits_t;
        m_Stream.fill(traits_t::zero);
    }
    //! Copy constructor
    file_counter_formatter(file_counter_formatter const& that)
      : m_FileCounterPosition(that.m_FileCounterPosition), m_Width(that.m_Width)
    {
        m_Stream.fill(that.m_Stream.fill());
    }
    //! The function formats the file counter into the file name
    path_string_type operator()(path_string_type const& pattern, unsigned int counter) const
    {
        path_string_type file_name = pattern;

        m_Stream.str(path_string_type());
        m_Stream.width(m_Width);
        m_Stream << counter;
        file_name.insert(m_FileCounterPosition, m_Stream.str());

        return file_name;
    }
    BOOST_DELETED_FUNCTION(file_counter_formatter& operator=(file_counter_formatter const&))
};

//! The function parses the format placeholder for file counter
bool parse_counter_placeholder(
    path_string_type::const_iterator& it, path_string_type::const_iterator end, unsigned int& width)
{
    typedef boost::spirit::qi::extract_uint<unsigned int, 10, 1, -1> width_extract;
    typedef file_char_traits<path_char_type> traits_t;
    if (it == end)
    {
        return false;
    }
    path_char_type c = *it;
    if (c == traits_t::zero || c == traits_t::space || c == traits_t::plus || c == traits_t::minus)
    {
        // Skip filler and alignment specification
        ++it;
        if (it == end)
        {
            return false;
        }
        c = *it;
    }
    if (traits_t::is_digit(c))
    {
        // Parse width
        if (!width_extract::call(it, end, width))
        {
            return false;
        }
        if (it == end)
        {
            return false;
        }
        c = *it;
    }
    if (c == traits_t::dot)
    {
        // Skip precision
        ++it;
        while (it != end && traits_t::is_digit(*it))
        {
            ++it;
        }
        if (it == end)
        {
            return false;
        }
        c = *it;
    }
    if (c == traits_t::number_placeholder)
    {
        ++it;
        return true;
    }
    return false;
}

//! The function matches the file name and the pattern
bool match_pattern(path_string_type const& file_name, path_string_type const& pattern,
    unsigned int& file_counter, bool& file_counter_parsed)
{
    typedef boost::spirit::qi::extract_uint<unsigned int, 10, 1, -1> file_counter_extract;
    typedef file_char_traits<path_char_type> traits_t;

    struct local
    {  // Verifies that the string contains exactly n digits
        static bool scan_digits(path_string_type::const_iterator& it,
            path_string_type::const_iterator end, std::ptrdiff_t n)
        {
            for (; n > 0; --n)
            {
                if (it == end)
                {
                    return false;
                }
                path_char_type c = *it++;
                if (!traits_t::is_digit(c))
                {
                    return false;
                }
            }
            return true;
        }
    };

    path_string_type::const_iterator f_it = file_name.begin();
    path_string_type::const_iterator f_end = file_name.end();
    path_string_type::const_iterator p_it = pattern.begin();
    path_string_type::const_iterator p_end = pattern.end();
    bool placeholder_expected = false;
    while (f_it != f_end && p_it != p_end)
    {
        path_char_type p_c = *p_it;
        path_char_type f_c = *f_it;
        if (!placeholder_expected)
        {
            if (p_c == traits_t::percent)
            {
                placeholder_expected = true;
                ++p_it;
            }
            else if (p_c == f_c)
            {
                ++p_it;
                ++f_it;
            }
            else
            {
                return false;
            }
        }
        else
        {
            switch (p_c)
            {
            case traits_t::percent:  // An escaped '%'
                if (p_c == f_c)
                {
                    ++p_it;
                    ++f_it;
                    break;
                }
                else
                {
                    return false;
                }
            case traits_t::seconds_placeholder:  // Date/time components with 2-digits width
            case traits_t::minutes_placeholder:
            case traits_t::hours_placeholder:
            case traits_t::day_placeholder:
            case traits_t::month_placeholder:
            case traits_t::year_placeholder:
                if (!local::scan_digits(f_it, f_end, 2))
                {
                    return false;
                }
                ++p_it;
                break;

            case traits_t::full_year_placeholder:  // Date/time components with 4-digits width
                if (!local::scan_digits(f_it, f_end, 4))
                {
                    return false;
                }
                ++p_it;
                break;

            case traits_t::frac_sec_placeholder:  // Fraction seconds width is
                                                  // configuration-dependent
                typedef boost::posix_time::time_res_traits posix_resolution_traits;
                if (!local::scan_digits(
                        f_it, f_end, posix_resolution_traits::num_fractional_digits()))
                {
                    return false;
                }
                ++p_it;
                break;

            default:  // This should be the file counter placeholder or some unsupported placeholder
            {
                path_string_type::const_iterator p = p_it;
                unsigned int width = 0;
                if (!parse_counter_placeholder(p, p_end, width))
                {
                    BOOST_THROW_EXCEPTION(std::invalid_argument(
                        "Unsupported placeholder used in pattern for file scanning"));
                }
                // Find where the file number ends
                path_string_type::const_iterator f = f_it;
                if (!local::scan_digits(f, f_end, width))
                {
                    return false;
                }
                while (f != f_end && traits_t::is_digit(*f))
                {
                    ++f;
                }

                if (!file_counter_extract::call(f_it, f, file_counter))
                {
                    return false;
                }

                file_counter_parsed = true;
                p_it = p;
            }
            break;
            }

            placeholder_expected = false;
        }
    }
    if (p_it == p_end)
    {
        if (f_it != f_end)
        {  // The actual file name may end with an additional counter
            // that is added by the collector in case if file name clash
            return local::scan_digits(f_it, f_end, std::distance(f_it, f_end));
        }
        return true;
    }
    return false;
}

//! A possible Boost.Filesystem extension - renames or moves the file to the target storage
inline void move_file(boost::filesystem::path const& from, boost::filesystem::path const& to)
{
#if defined(BOOST_WINDOWS_API)
    // On Windows MoveFile already does what we need
    boost::filesystem::rename(from, to);
#else
    // On POSIX rename fails if the target points to a different device

    boost::system::error_code ec;
    boost::filesystem::rename(from, to, ec);
    if (ec)
    {
        if (BOOST_LIKELY(ec.value() == boost::system::errc::cross_device_link))
        {
            // Attempt to manually move the file instead
            boost::filesystem::copy_file(from, to);
            boost::filesystem::remove(from);
        }
        else
        {
            BOOST_THROW_EXCEPTION(
                boost::system::system_error(ec, "failed to move file to another location"));
        }
    }
#endif
}

inline void convert_to_tar_gz(
    boost::filesystem::path const& from, boost::filesystem::path const& to)
{
    bcos::createGzFile(from.string(), to.string());
    boost::filesystem::remove(from);
}

class file_collector_repository;

//! Type of the hook used for sequencing file collectors
using file_collector_hook =
    boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>>;

//! Log file collector implementation
class file_collector : public boost::log::sinks::file::collector,
                       public file_collector_hook,
                       public boost::enable_shared_from_this<file_collector>
{
private:
    //! Information about a single stored file
    struct file_info
    {
        //! Ordering predicate by timestamp
        struct order_by_timestamp
        {
            using result_type = bool;

            result_type operator()(
                file_info const& left, file_info const& right) const BOOST_NOEXCEPT
            {
                return left.m_TimeStamp < right.m_TimeStamp;
            }
        };

        //! Predicate for testing if a file_info refers to a file equivalent to another path
        class equivalent_file
        {
        public:
            using result_type = bool;
            explicit equivalent_file(boost::filesystem::path const& path) BOOST_NOEXCEPT
              : m_Path(path)
            {}

            result_type operator()(file_info const& info) const
            {
                return boost::filesystem::equivalent(info.m_Path, m_Path);
            }

        private:
            boost::filesystem::path const& m_Path;
        };

        uintmax_t m_Size;
        std::time_t m_TimeStamp;
        boost::filesystem::path m_Path;
    };
    //! A list of the stored files
    using file_list = std::list<file_info>;
    //! The string type compatible with the universal path type
    using path_string_type = boost::filesystem::path::string_type;

    //! A reference to the repository this collector belongs to
    boost::shared_ptr<file_collector_repository> m_pRepository;

#if !defined(BOOST_LOG_NO_THREADS)
    //! Synchronization mutex
    std::mutex m_Mutex;
#endif  // !defined(BOOST_LOG_NO_THREADS)

    //! Total file size upper limit
    uintmax_t m_MaxSize;
    //! Free space lower limit
    uintmax_t m_MinFreeSpace;
    //! File count upper limit
    uintmax_t m_MaxFiles;

    //! The current path at the point when the collector is created
    /*
     * The special member is required to calculate absolute paths with no
     * dependency on the current path for the application, which may change
     */
    const boost::filesystem::path m_BasePath;
    //! Target directory to store files to
    boost::filesystem::path m_StorageDir;

    //! The list of stored files
    file_list m_Files;
    //! Total size of the stored files
    uintmax_t m_TotalSize;
    bool m_ConvertTarGZ = false;

public:
    //! Constructor
    file_collector(boost::shared_ptr<file_collector_repository> repo,
        boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
        uintmax_t max_files);

    //! Destructor
    ~file_collector() BOOST_OVERRIDE;

    //! The function stores the specified file in the storage
    void store_file(boost::filesystem::path const& src_path) BOOST_OVERRIDE;

    //! The function checks if the specified path refers to an existing file in the storage
    bool is_in_storage(boost::filesystem::path const& src_path) const BOOST_OVERRIDE;

    //! Scans the target directory for the files that have already been stored
    boost::log::sinks::file::scan_result scan_for_files(boost::log::sinks::file::scan_method method,
        boost::filesystem::path const& pattern) BOOST_OVERRIDE;

    //! The function updates storage restrictions
    void update(uintmax_t max_size, uintmax_t min_free_space, uintmax_t max_files);

    //! The function checks if the directory is governed by this collector
    bool is_governed(boost::filesystem::path const& dir) const
    {
        return boost::filesystem::equivalent(m_StorageDir, dir);
    }

    void convert_tar_gz(bool ConvertTarGZ) { m_ConvertTarGZ = ConvertTarGZ; }

private:
    //! Makes relative path absolute with respect to the base path
    boost::filesystem::path make_absolute(boost::filesystem::path const& path) const
    {
        return boost::filesystem::absolute(path, m_BasePath);
    }
    //! Acquires file name string from the path
    static path_string_type filename_string(boost::filesystem::path const& path)
    {
        return path.filename().string<path_string_type>();
    }
};


//! The singleton of the list of file collectors
class file_collector_repository : public boost::log::aux::lazy_singleton<file_collector_repository,
                                      boost::shared_ptr<file_collector_repository>>
{
private:
    //! Base type
    using base_type = boost::log::aux::lazy_singleton<file_collector_repository,
        boost::shared_ptr<file_collector_repository>>;

#if !defined(BOOST_LOG_BROKEN_FRIEND_TEMPLATE_SPECIALIZATIONS)
    friend class boost::log::aux::lazy_singleton<file_collector_repository,
        boost::shared_ptr<file_collector_repository>>;
#else
    friend class base_type;
#endif

    //! The type of the list of collectors
    using file_collectors =
        boost::intrusive::list<file_collector, boost::intrusive::base_hook<file_collector_hook>>;

#if !defined(BOOST_LOG_NO_THREADS)
    //! Synchronization mutex
    std::mutex m_Mutex;
#endif  // !defined(BOOST_LOG_NO_THREADS)
        //! The list of file collectors
    file_collectors m_Collectors;

public:
    //! Finds or creates a file collector
    boost::shared_ptr<boost::log::sinks::file::collector> get_collector(
        boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
        uintmax_t max_files, bool convert_tar_gz);

    //! Removes the file collector from the list
    void remove_collector(file_collector* fileCollector);

private:
    //! Initializes the singleton instance
    static void init_instance()
    {
        base_type::get_instance() = boost::make_shared<file_collector_repository>();
    }
};

//! Constructor
file_collector::file_collector(boost::shared_ptr<file_collector_repository> repo,
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files)
  : m_pRepository(std::move(repo)),
    m_MaxSize(max_size),
    m_MinFreeSpace(min_free_space),
    m_MaxFiles(max_files),
    m_BasePath(boost::filesystem::current_path()),
    m_TotalSize(0)
{
    m_StorageDir = make_absolute(target_dir);
    boost::filesystem::create_directories(m_StorageDir);
}

//! Destructor
file_collector::~file_collector()
{
    m_pRepository->remove_collector(this);
}

//! The function stores the specified file in the storage
void file_collector::store_file(boost::filesystem::path const& src_path)
{
    // NOTE FOR THE FOLLOWING CODE:
    // Avoid using Boost.Filesystem functions that would call path::codecvt(). store_file() can be
    // called at process termination, and the global codecvt facet can already be destroyed at this
    // point. https://svn.boost.org/trac/boost/ticket/8642

    // Let's construct the new file name
    file_info info;
    info.m_TimeStamp = boost::filesystem::last_write_time(src_path);
    info.m_Size = boost::filesystem::file_size(src_path);

    const boost::filesystem::path file_name_path = src_path.filename();
    path_string_type const& file_name = file_name_path.native();
    info.m_Path = m_StorageDir / file_name_path;

    // Check if the file is already in the target directory
    boost::filesystem::path src_dir =
        src_path.has_parent_path() ? boost::filesystem::system_complete(src_path.parent_path()) :
                                     m_BasePath;
    const bool is_in_target_dir = boost::filesystem::equivalent(src_dir, m_StorageDir);
    if (!is_in_target_dir)
    {
        if (boost::filesystem::exists(info.m_Path))
        {
            // If the file already exists, try to mangle the file name
            // to ensure there's no conflict. I'll need to make this customizable some day.
            file_counter_formatter formatter(file_name.size(), 5);
            unsigned int n = 0;
            while (true)
            {
                path_string_type alt_file_name = formatter(file_name, n);
                info.m_Path = m_StorageDir / boost::filesystem::path(alt_file_name);
                if (!boost::filesystem::exists(info.m_Path))
                {
                    break;
                }

                if (BOOST_UNLIKELY(n == (std::numeric_limits<unsigned int>::max)()))
                {
                    BOOST_THROW_EXCEPTION(boost::system::system_error(boost::system::error_code{},
                        "Target file exists and an unused fallback file name could not be "
                        "found"));
                }

                ++n;
            }
        }

        // The directory should have been created in constructor, but just in case it got deleted
        // since then...
        boost::filesystem::create_directories(m_StorageDir);
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_Files.begin();
    const auto end = m_Files.end();
    if (is_in_target_dir)
    {
        // If the sink writes log file into the target dir (is_in_target_dir == true), it is
        // possible that after scanning an old file entry refers to the file that is picked up by
        // the sink for writing. Later on, the sink attempts to store the file in the storage. At
        // best, this would result in duplicate file entries. At worst, if the storage limits
        // trigger a deletion and this file get deleted, we may have an entry that refers to no
        // actual file. In any case, the total size of files in the storage will be incorrect. Here
        // we work around this problem and simply remove the old file entry without removing the
        // file. The entry will be re-added to the list later.
        while (it != end)
        {
            boost::system::error_code ec;
            if (boost::filesystem::equivalent(it->m_Path, info.m_Path, ec))
            {
                m_TotalSize -= it->m_Size;
                m_Files.erase(it);
                break;
            }
            ++it;
        }

        it = m_Files.begin();
    }

    // Check if an old file should be erased
    uintmax_t free_space = m_MinFreeSpace != 0U ? boost::filesystem::space(m_StorageDir).available :
                                                  static_cast<uintmax_t>(0);
    while (it != end && (m_TotalSize + info.m_Size > m_MaxSize ||
                            ((m_MinFreeSpace != 0U) && m_MinFreeSpace > free_space) ||
                            m_MaxFiles <= m_Files.size()))
    {
        file_info& old_info = *it;
        boost::system::error_code ec;
        boost::filesystem::file_status status = boost::filesystem::status(old_info.m_Path, ec);

        if (status.type() == boost::filesystem::regular_file)
        {
            try
            {
                boost::filesystem::remove(old_info.m_Path);
                // Free space has to be queried as it may not increase equally
                // to the erased file size on compressed filesystems
                if (m_MinFreeSpace != 0U)
                {
                    free_space = boost::filesystem::space(m_StorageDir).available;
                }
                m_TotalSize -= old_info.m_Size;
                it = m_Files.erase(it);
            }
            catch (boost::system::system_error&)
            {
                // Can't erase the file. Maybe it's locked? Never mind...
                ++it;
            }
        }
        else
        {
            // If it's not a file or is absent, just remove it from the list
            m_TotalSize -= old_info.m_Size;
            it = m_Files.erase(it);
        }
    }

    if (!is_in_target_dir)
    {
        // Move/rename the file to the target storage
        move_file(src_path, info.m_Path);
    }
    if (m_ConvertTarGZ)
    {
        auto compressedPath = info.m_Path;
        compressedPath += ".gz";
        convert_to_tar_gz(info.m_Path, compressedPath);
        info.m_Path = std::move(compressedPath);
        info.m_Size = boost::filesystem::file_size(info.m_Path);
    }

    m_TotalSize += info.m_Size;
    m_Files.push_back(std::move(info));
}

//! The function checks if the specified path refers to an existing file in the storage
bool file_collector::is_in_storage(boost::filesystem::path const& src_path) const
{
    const boost::filesystem::path file_name_path = src_path.filename();
    const boost::filesystem::path trg_path = m_StorageDir / file_name_path;

    // Check if the file is already in the target directory
    boost::system::error_code ec;
    boost::filesystem::path src_dir =
        src_path.has_parent_path() ?
            boost::filesystem::system_complete(src_path.parent_path(), ec) :
            m_BasePath;
    if (ec)
    {
        return false;
    }

    boost::filesystem::file_status status = boost::filesystem::status(trg_path, ec);
    if (ec || status.type() != boost::filesystem::regular_file)
    {
        return false;
    }
    bool equiv = boost::filesystem::equivalent(src_dir / file_name_path, trg_path, ec);
    if (ec)
    {
        return false;
    }

    return equiv;
}

//! Scans the target directory for the files that have already been stored
boost::log::sinks::file::scan_result file_collector::scan_for_files(
    boost::log::sinks::file::scan_method method, boost::filesystem::path const& pattern)
{
    boost::log::sinks::file::scan_result result;
    if (method != boost::log::sinks::file::no_scan)
    {
        boost::filesystem::path dir = m_StorageDir;
        path_string_type mask;
        if (method == boost::log::sinks::file::scan_matching)
        {
            if (m_ConvertTarGZ)
            {
                auto filename = pattern.filename();
                filename.append(".gz");
                mask = filename_string(filename);
            }
            else
            {
                mask = filename_string(pattern);
            }
            if (pattern.has_parent_path())
            {
                dir = make_absolute(pattern.parent_path());
            }
        }

        boost::system::error_code ec;
        boost::filesystem::file_status status = boost::filesystem::status(dir, ec);
        if (status.type() == boost::filesystem::directory_file)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            file_list files;

            boost::filesystem::directory_iterator it(dir);
            boost::filesystem::directory_iterator end;
            uintmax_t total_size = 0U;
            for (; it != end; ++it)
            {
                boost::filesystem::directory_entry const& dir_entry = *it;
                file_info info;
                info.m_Path = dir_entry.path();
                status = dir_entry.status(ec);
                if (status.type() == boost::filesystem::regular_file)
                {
                    // Check that there are no duplicates in the resulting list
                    if (std::find_if(m_Files.begin(), m_Files.end(),
                            file_info::equivalent_file(info.m_Path)) == m_Files.end())
                    {
                        // Check that the file name matches the pattern
                        unsigned int file_number = 0U;
                        bool file_number_parsed = false;
                        if (method != boost::log::sinks::file::scan_matching ||
                            match_pattern(filename_string(info.m_Path), mask, file_number,
                                file_number_parsed))
                        {
                            info.m_Size = boost::filesystem::file_size(info.m_Path);
                            total_size += info.m_Size;
                            info.m_TimeStamp = boost::filesystem::last_write_time(info.m_Path);
                            files.push_back(std::move(info));
                            ++result.found_count;

                            // Test that the file_number >= result.last_file_counter accounting for
                            // the integer overflow
                            if (file_number_parsed &&
                                (!result.last_file_counter ||
                                    (file_number - *result.last_file_counter) <
                                        ((~0U) ^ ((~0U) >> 1U))))
                            {
                                result.last_file_counter = file_number;
                            }
                        }
                    }
                }
            }

            // Sort files chronologically
            m_Files.splice(m_Files.end(), files);
            m_TotalSize += total_size;
            m_Files.sort(file_info::order_by_timestamp());
        }
    }

    return result;
}

//! The function updates storage restrictions
void file_collector::update(uintmax_t max_size, uintmax_t min_free_space, uintmax_t max_files)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    m_MaxSize = (std::min)(m_MaxSize, max_size);
    m_MinFreeSpace = (std::max)(m_MinFreeSpace, min_free_space);
    m_MaxFiles = (std::min)(m_MaxFiles, max_files);
}


//! Finds or creates a file collector
boost::shared_ptr<boost::log::sinks::file::collector> file_collector_repository::get_collector(
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files, bool convert_tar_gz)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    file_collectors::iterator it = std::find_if(
        m_Collectors.begin(), m_Collectors.end(), [&target_dir](file_collector const& collector) {
            return collector.is_governed(target_dir);
        });
    boost::shared_ptr<file_collector> collector;
    if (it != m_Collectors.end())
    {
        try
        {
            // This may throw if the collector is being currently destroyed
            collector = it->shared_from_this();
            collector->update(max_size, min_free_space, max_files);
        }
        catch (std::bad_weak_ptr&)
        {}
    }

    if (!collector)
    {
        collector = boost::make_shared<file_collector>(
            file_collector_repository::get(), target_dir, max_size, min_free_space, max_files);
        m_Collectors.push_back(*collector);
    }
    collector->convert_tar_gz(convert_tar_gz);
    return collector;
}

//! Removes the file collector from the list
void file_collector_repository::remove_collector(file_collector* fileCollector)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Collectors.erase(m_Collectors.iterator_to(*fileCollector));
}
}  // namespace
namespace log
{

boost::shared_ptr<boost::log::sinks::file::collector> make_collector(
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files = (std::numeric_limits<uintmax_t>::max)(), bool convert_tar_gz = false)
{
    return file_collector_repository::get()->get_collector(
        target_dir, max_size, min_free_space, max_files, convert_tar_gz);
}
}  // namespace log
}  // namespace bcos
