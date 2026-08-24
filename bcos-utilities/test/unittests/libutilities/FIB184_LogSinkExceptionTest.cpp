/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief FIB-184: an exception thrown on an asynchronous log sink's feeding thread must not
 *        propagate to std::terminate/abort. BoostLogInitializer installs
 *        make_exception_suppressor() on every async sink; this verifies that mechanism: a sink
 *        backend that always throws does not crash the process when the suppressor is installed.
 * @file FIB184_LogSinkExceptionTest.cpp
 */

#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stdexcept>

namespace bcos::test
{
namespace
{
// A sink backend that throws on every consumed record, simulating a formatting/IO failure on the
// async sink's dedicated feeding thread (the FIB-184 crash trigger).
class ThrowingBackend
  : public boost::log::sinks::basic_sink_backend<boost::log::sinks::synchronized_feeding>
{
public:
    std::atomic<int>* m_consumed = nullptr;
    void consume(boost::log::record_view const& /*record*/)
    {
        if (m_consumed != nullptr)
        {
            m_consumed->fetch_add(1);
        }
        throw std::runtime_error("FIB-184 simulated log backend failure");
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FIB184LogSinkExceptionTest)

BOOST_AUTO_TEST_CASE(asyncSinkSuppressesBackendException)
{
    using sink_t = boost::log::sinks::asynchronous_sink<ThrowingBackend>;
    std::atomic<int> consumed{0};
    auto backend = boost::make_shared<ThrowingBackend>();
    backend->m_consumed = &consumed;
    auto sink = boost::make_shared<sink_t>(backend);
    // The fix under test: without this handler the throw on the feeding thread is uncaught and
    // reaches std::terminate -> abort (it would crash this test binary, not just fail the case).
    sink->set_exception_handler(boost::log::make_exception_suppressor());

    auto core = boost::log::core::get();
    core->add_sink(sink);

    boost::log::sources::severity_logger<int> logger;
    BOOST_LOG_SEV(logger, 0) << "FIB-184 trigger record";

    sink->stop();  // join the feeding thread: consume() runs here, throws, and is suppressed
    sink->flush();
    core->remove_sink(sink);

    BOOST_CHECK_GE(consumed.load(), 1);  // the throwing backend actually ran
    BOOST_CHECK(true);                   // reached only because the throw did not abort the process
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
