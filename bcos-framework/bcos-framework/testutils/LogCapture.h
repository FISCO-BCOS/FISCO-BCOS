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
#include <boost/log/core.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <sstream>
#include <string>

namespace bcos::test
{
class LogCapture
{
public:
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;

    LogCapture() : m_stream(boost::make_shared<std::ostringstream>())
    {
        auto backend = boost::make_shared<boost::log::sinks::text_ostream_backend>();
        backend->add_stream(m_stream);
        backend->auto_flush(true);
        m_sink = boost::make_shared<Sink>(backend);
        boost::log::core::get()->add_sink(m_sink);
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
            boost::log::core::get()->remove_sink(m_sink);
            m_sink->flush();
            m_sink.reset();
        }
    }

    boost::shared_ptr<std::ostringstream> m_stream;
    boost::shared_ptr<Sink> m_sink;
};
}  // namespace bcos::test
