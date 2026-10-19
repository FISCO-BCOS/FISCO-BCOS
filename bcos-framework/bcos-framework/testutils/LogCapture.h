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
        void writeLine(LogLevel, std::string_view, std::string_view _message) override
        {
            *m_stream << _message << '\n';
        }

    private:
        std::shared_ptr<std::ostringstream> m_stream;
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
        stop();
        return m_stream->str();
    }

private:
    void stop()
    {
        if (m_sink)
        {
            log::unregisterLineSink(m_sink.get());
            m_sink.reset();
        }
    }

    std::shared_ptr<std::ostringstream> m_stream;
    std::shared_ptr<CaptureSink> m_sink;
};
}  // namespace bcos::test
