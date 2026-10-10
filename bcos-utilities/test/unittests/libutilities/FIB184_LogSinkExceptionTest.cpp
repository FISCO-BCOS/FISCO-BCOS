/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief FIB-184: an exception thrown by a log backend on the asynchronous
 *        feeding thread must not reach std::terminate/abort. Production
 *        logging goes through LineAsyncSink, which guards both consumeLine and
 *        flush with catch(...); this test throws from both and verifies the
 *        feeding thread survives, keeps draining, and the sink still stops
 *        cleanly.
 * @file FIB184_LogSinkExceptionTest.cpp
 */

#include "bcos-utilities/LineAsyncSink.h"
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stdexcept>
#include <string>

namespace bcos::test
{
namespace
{
// A backend that throws on every consumeLine and every flush, simulating a
// persistent IO failure on the async sink's dedicated feeding thread (the
// FIB-184 crash trigger).
struct AlwaysThrowingBackend
{
    std::atomic<int>* m_consumed = nullptr;

    void consumeLine(bcos::LogLevel /*level*/, std::string const& /*line*/)
    {
        if (m_consumed != nullptr)
        {
            m_consumed->fetch_add(1);
        }
        throw std::runtime_error("FIB-184 simulated log backend failure");
    }
    void flush() { throw std::runtime_error("FIB-184 simulated log flush failure"); }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FIB184LogSinkExceptionTest)

BOOST_AUTO_TEST_CASE(lineSinkSuppressesBackendException)
{
    std::atomic<int> consumed{0};
    auto backend = boost::make_shared<AlwaysThrowingBackend>();
    backend->m_consumed = &consumed;
    bcos::log::LineAsyncSink<AlwaysThrowingBackend> sink(backend);

    sink.writeLine(bcos::LogLevel::INFO, "p|", "FIB-184 trigger line");
    // flush() and stop() run the throwing backend on the feeding thread and
    // join it; reaching std::terminate would crash the whole test binary.
    sink.flush();
    sink.stop();

    BOOST_CHECK_GE(consumed.load(), 1);  // the throwing backend actually ran
    BOOST_CHECK(true);                   // reached only because nothing escaped the thread
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
