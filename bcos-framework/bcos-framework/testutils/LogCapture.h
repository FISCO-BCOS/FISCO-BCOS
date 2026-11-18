/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief test helper: collects the message text of every log record emitted while it is alive,
 *        so a test can assert on the exact line a code path prints
 * @file LogCapture.h
 */
#pragma once
#include <bcos-utilities/LineAsyncSink.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace bcos::test
{
// The logging pipeline no longer goes through boost::log core, so capturing
// means registering a whole-line sink: LogStream commits each formatted line
// to every registered sink on destruction.
class LogCapture
{
private:
    class CaptureSink : public log::LineSinkWriter
    {
    public:
        explicit CaptureSink(std::shared_ptr<std::ostringstream> _stream)
          : m_stream(std::move(_stream))
        {}
        // commitLine fans out under a shared lock, so producers on different
        // threads reach this concurrently — serialize the insertion, as the
        // base's synchronous_sink did.
        //
        // Signature must match LineSinkWriter::writeLine exactly, including the
        // optional LineRecord parameter: a default argument only lets CALLERS
        // omit it, it does not let a three-parameter function override a
        // four-parameter virtual. This sink renders the whole line itself and has
        // no queue to stamp, so it ignores the record.
        void writeLine(LogLevel, std::string_view, std::string_view _message,
            LineRecord const* = nullptr) override
        {
            std::lock_guard lock(m_mutex);
            // closed is set by close() under this same mutex, so a producer that
            // loaded this sink from the registry just before unregisterLineSink()
            // can still arrive here, and its write must be rejected rather than
            // racing a later snapshot.
            if (m_closed)
            {
                return;
            }
            *m_stream << _message << '\n';
        }

        /// Stops accepting writes and returns everything collected so far, both
        /// under the write mutex. Every snapshot must go through here: reading the
        /// stream under a separate lock could race a concurrent writeLine().
        std::string closeAndSnapshot()
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
            return m_stream->str();
        }

    private:
        std::shared_ptr<std::ostringstream> m_stream;
        std::mutex m_mutex;
        bool m_closed = false;
    };

public:
    LogCapture() : m_stream(std::make_shared<std::ostringstream>())
    {
        m_sink = std::make_shared<CaptureSink>(m_stream);
        log::registerLineSink(m_sink);
    }
    ~LogCapture() { stop(); }
    LogCapture(LogCapture const&) = delete;
    LogCapture& operator=(LogCapture const&) = delete;

    /// detaches the sink (later records are not collected) and returns everything collected,
    /// one record per line
    std::string text()
    {
        if (m_sink)
        {
            // unregisterLineSink() only unpublishes the sink: it deliberately
            // does NOT wait for producers, unlike the registry it replaced. A
            // producer that already loaded the pointer can therefore still reach
            // writeLine() after this returns, which is why the snapshot has to be
            // the sink's own close-and-snapshot under its write mutex.
            auto sink = m_sink;
            log::unregisterLineSink(sink.get());
            m_sink.reset();
            auto text = sink->closeAndSnapshot();
            m_lastText = text;
            return text;
        }
        // Detached already: the sink is closed, so this is a stable value.
        return m_lastText;
    }

private:
    void stop()
    {
        if (m_sink)
        {
            auto sink = m_sink;
            log::unregisterLineSink(sink.get());
            m_sink.reset();
            m_lastText = sink->closeAndSnapshot();
        }
    }

    std::shared_ptr<std::ostringstream> m_stream;
    std::shared_ptr<CaptureSink> m_sink;
    // Snapshot taken when the sink was detached. Written only while releasing
    // m_sink, and read by text() once m_sink is clear; the sink's own mutex already
    // serialises the stream access, so no second mutex is required here.
    std::string m_lastText;
};
}  // namespace bcos::test
