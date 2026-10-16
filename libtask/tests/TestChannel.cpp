#include "bcos-task/Channel.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace bcos::task;

namespace
{
// Inline poster: the wake runs on the producer's stack. Tests that need the consumer on another
// thread spawn that thread themselves — the channel never resumes a waiter inline by itself.
Channel<int>::Poster inlinePoster()
{
    return [](std::function<void()> f) { f(); };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ChannelTest)

BOOST_AUTO_TEST_CASE(pushThenRecvKeepsOrder)
{
    Channel<int> channel(inlinePoster(), 16);
    channel.push(1);
    channel.push(2);
    channel.push(3);

    auto result = syncWait([](Channel<int>& ch) -> Task<std::vector<int>> {
        std::vector<int> values;
        for (int i = 0; i < 3; ++i)
        {
            values.push_back(co_await ch.recv());
        }
        co_return values;
    }(channel));

    const std::vector<int> expected{1, 2, 3};
    BOOST_CHECK(result == expected);
}

BOOST_AUTO_TEST_CASE(parkedRecvIsWokenByPushFromAnotherThread)
{
    Channel<int> channel(inlinePoster(), 16);
    std::promise<int> received;

    // task::wait runs the consumer synchronously up to its first suspension: it is parked in
    // recv() before this thread continues.
    wait([](Channel<int>& ch, std::promise<int>& out) -> Task<void> {
        out.set_value(co_await ch.recv());
    }(channel, received));

    std::thread producer([&channel]() { BOOST_CHECK(channel.push(42)); });
    BOOST_CHECK_EQUAL(received.get_future().get(), 42);
    producer.join();
}

BOOST_AUTO_TEST_CASE(closeWakesParkedRecvWithException)
{
    Channel<int> channel(inlinePoster(), 16);
    std::promise<std::string> failure;

    wait([](Channel<int>& ch, std::promise<std::string>& out) -> Task<void> {
        try
        {
            co_await ch.recv();
            out.set_value("no exception");
        }
        catch (const std::exception& e)
        {
            out.set_value(e.what());
        }
    }(channel, failure));

    channel.close(std::make_exception_ptr(std::runtime_error("peer gone")));
    BOOST_CHECK_EQUAL(failure.get_future().get(), "peer gone");

    // A second close does not override the first error, and recv keeps throwing after close.
    channel.close(std::make_exception_ptr(std::runtime_error("other")));
    auto result = syncWait([](Channel<int>& ch) -> Task<std::string> {
        try
        {
            co_await ch.recv();
        }
        catch (const std::exception& e)
        {
            co_return std::string(e.what());
        }
        co_return "no exception";
    }(channel));
    BOOST_CHECK_EQUAL(result, "peer gone");
}

BOOST_AUTO_TEST_CASE(pushFailsWhenFull)
{
    Channel<int> channel(inlinePoster(), 2);
    BOOST_CHECK(channel.push(1));
    BOOST_CHECK(channel.push(2));
    BOOST_CHECK(!channel.push(3));
    BOOST_CHECK_EQUAL(channel.size(), 2);

    // Draining one slot frees capacity again.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 1);
    BOOST_CHECK(channel.push(3));
}

BOOST_AUTO_TEST_CASE(byteBudgetIsHonoured)
{
    Channel<std::string> channel(inlinePoster(), 16, /*maxBytes=*/10,
        [](const std::string& value) { return value.size(); });
    BOOST_CHECK(channel.push("123456"));
    BOOST_CHECK(!channel.push("12345"));  // 6 + 5 > 10
    BOOST_CHECK(channel.push("1234"));    // 6 + 4 == 10
}

BOOST_AUTO_TEST_CASE(queuedValuesDrainAfterClose)
{
    Channel<int> channel(inlinePoster(), 16);
    channel.push(7);
    channel.push(8);
    channel.close();
    BOOST_CHECK(!channel.push(9));  // closed channels reject new values

    auto result = syncWait([](Channel<int>& ch) -> Task<std::vector<int>> {
        std::vector<int> values;
        try
        {
            while (true)
            {
                values.push_back(co_await ch.recv());
            }
        }
        catch (const std::exception&)
        {
        }
        co_return values;
    }(channel));

    const std::vector<int> expected{7, 8};
    BOOST_CHECK(result == expected);
}

BOOST_AUTO_TEST_CASE(waitWritableReturnsImmediatelyWhenSpaceAvailable)
{
    Channel<int> channel(inlinePoster(), 2);
    BOOST_CHECK(syncWait(channel.waitWritable()));
    channel.push(1);
    BOOST_CHECK(syncWait(channel.waitWritable()));
}

BOOST_AUTO_TEST_CASE(parkedProducerWokenByConsumerDrain)
{
    Channel<int> channel(inlinePoster(), 2);
    channel.push(1);
    channel.push(2);

    std::promise<bool> writable;
    // wait() runs the producer synchronously up to its first suspension: the channel is full,
    // so it is parked in waitWritable() before this thread continues.
    wait([](Channel<int>& ch, std::promise<bool>& out) -> Task<void> {
        out.set_value(co_await ch.waitWritable());
        BOOST_CHECK(ch.push(3));
    }(channel, writable));
    auto future = writable.get_future();
    BOOST_CHECK(!future.valid() || future.wait_for(std::chrono::milliseconds(0)) !=
                                     std::future_status::ready);

    // Draining one slot frees exactly the space the producer is waiting for; the wake arrives
    // through the (inline) poster.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 1);
    BOOST_CHECK(future.get());
    BOOST_CHECK_EQUAL(channel.size(), 2);  // 2 and 3 queued
}

BOOST_AUTO_TEST_CASE(parkedProducerWokenOnlyWhenItsBytesFit)
{
    Channel<std::string> channel(inlinePoster(), 16, /*maxBytes=*/10,
        [](const std::string& value) { return value.size(); });
    BOOST_CHECK(channel.push("12345"));
    BOOST_CHECK(channel.push("12345"));  // 10/10 bytes queued

    std::promise<bool> writable;
    wait([](Channel<std::string>& ch, std::promise<bool>& out) -> Task<void> {
        out.set_value(co_await ch.waitWritable(6));
    }(channel, writable));
    auto future = writable.get_future();

    // Freeing 5 bytes is not enough for a 6-byte push: the producer stays parked.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), "12345");
    BOOST_CHECK(future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);

    // Freeing the rest wakes it.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), "12345");
    BOOST_CHECK(future.get());
}

BOOST_AUTO_TEST_CASE(closeWakesParkedProducerWithFalse)
{
    Channel<int> channel(inlinePoster(), 1);
    channel.push(1);

    std::promise<bool> writable;
    wait([](Channel<int>& ch, std::promise<bool>& out) -> Task<void> {
        out.set_value(co_await ch.waitWritable());
    }(channel, writable));

    channel.close();
    BOOST_CHECK(!writable.get_future().get());

    // A closed channel answers immediately.
    BOOST_CHECK(!syncWait(channel.waitWritable()));
}

BOOST_AUTO_TEST_SUITE_END()
