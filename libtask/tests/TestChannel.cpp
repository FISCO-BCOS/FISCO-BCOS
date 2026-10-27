#include "bcos-task/Channel.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
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
    bcos::task::wait([](Channel<int>& ch, std::promise<int>& out) -> Task<void> {
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

    bcos::task::wait([](Channel<int>& ch, std::promise<std::string>& out) -> Task<void> {
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

// A failed push leaves the value INTACT: the multi-producer loop the class comment mandates
// (push → waitWritable → retry the same item) would lose the message otherwise.
BOOST_AUTO_TEST_CASE(failedPushPreservesTheValue)
{
    Channel<std::string> channel(inlinePoster(), 1);
    BOOST_CHECK(channel.push(std::string("first")));

    auto value = std::string("second");
    BOOST_CHECK(!channel.push(value));  // full: the value must survive the failed push
    BOOST_CHECK_EQUAL(value, "second");

    BOOST_CHECK_EQUAL(syncWait(channel.recv()), "first");
    BOOST_CHECK(channel.push(value));  // retrying the SAME value now succeeds
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), "second");
}

BOOST_AUTO_TEST_CASE(byteBudgetIsHonoured)
{
    Channel<std::string> channel(
        inlinePoster(), 16, /*maxBytes=*/10, [](const std::string& value) { return value.size(); });
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
        {}
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
    bcos::task::wait([](Channel<int>& ch, std::promise<bool>& out) -> Task<void> {
        out.set_value(co_await ch.waitWritable());
        BOOST_CHECK(ch.push(3));
    }(channel, writable));
    auto future = writable.get_future();
    BOOST_CHECK(!future.valid() ||
                future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);

    // Draining one slot frees exactly the space the producer is waiting for; the wake arrives
    // through the (inline) poster.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 1);
    BOOST_CHECK(future.get());
    BOOST_CHECK_EQUAL(channel.size(), 2);  // 2 and 3 queued
}

BOOST_AUTO_TEST_CASE(parkedProducerWokenOnlyWhenItsBytesFit)
{
    Channel<std::string> channel(
        inlinePoster(), 16, /*maxBytes=*/10, [](const std::string& value) { return value.size(); });
    BOOST_CHECK(channel.push("12345"));
    BOOST_CHECK(channel.push("12345"));  // 10/10 bytes queued

    std::promise<bool> writable;
    bcos::task::wait([](Channel<std::string>& ch, std::promise<bool>& out) -> Task<void> {
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
    bcos::task::wait([](Channel<int>& ch, std::promise<bool>& out) -> Task<void> {
        out.set_value(co_await ch.waitWritable());
    }(channel, writable));

    channel.close();
    BOOST_CHECK(!writable.get_future().get());

    // A closed channel answers immediately.
    BOOST_CHECK(!syncWait(channel.waitWritable()));
}

BOOST_AUTO_TEST_CASE(byteBudgetIsReleasedOnPopWithoutSizeOf)
{
    // sizeOf defaults to 1-per-item: with a byte budget but no sizeOf, every item counts as one
    // byte, and popping must release it again.
    Channel<int> channel(inlinePoster(), 100, /*maxBytes=*/50);
    for (int i = 0; i < 200; ++i)
    {
        BOOST_CHECK(channel.push(i));
        BOOST_CHECK_EQUAL(syncWait(channel.recv()), i);
    }
}

BOOST_AUTO_TEST_CASE(waitWritableFailsFastWhenCountCapIsZero)
{
    Channel<int> channel(inlinePoster(), 0);
    BOOST_CHECK(!channel.push(1));
    // A zero maxCount rejects every push: parking would wait for a pop that can never help.
    BOOST_CHECK(!syncWait(channel.waitWritable()));
}

BOOST_AUTO_TEST_CASE(waitWritableFailsFastWhenPushExceedsWholeBudget)
{
    Channel<std::string> channel(
        inlinePoster(), 16, /*maxBytes=*/10, [](const std::string& value) { return value.size(); });
    // An item bigger than the whole byte budget can never fit, however much is drained.
    BOOST_CHECK(!syncWait(channel.waitWritable(11)));
    // The channel stays usable for pushes that do fit.
    BOOST_CHECK(channel.push("12345"));
    BOOST_CHECK(syncWait(channel.waitWritable(5)));
}

BOOST_AUTO_TEST_CASE(secondRecvDisplacesAndFailsFirstWaiter)
{
    Channel<int> channel(inlinePoster(), 16);
    std::promise<std::string> first;
    std::promise<int> second;

    bcos::task::wait([](Channel<int>& ch, std::promise<std::string>& out) -> Task<void> {
        try
        {
            co_await ch.recv();
            out.set_value("no exception");
        }
        catch (const std::exception& e)
        {
            out.set_value(e.what());
        }
    }(channel, first));

    // A second parked recv() displaces the first: it is woken with an exception through the
    // (inline) poster instead of being stranded.
    bcos::task::wait([](Channel<int>& ch, std::promise<int>& out) -> Task<void> {
        out.set_value(co_await ch.recv());
    }(channel, second));

    BOOST_CHECK_EQUAL(first.get_future().get(), "channel waiter displaced by a second recv()");
    BOOST_CHECK(channel.push(7));
    BOOST_CHECK_EQUAL(second.get_future().get(), 7);
}

BOOST_AUTO_TEST_CASE(parkedProducersWakeInFifoOrder)
{
    Channel<int> channel(inlinePoster(), 1);
    BOOST_CHECK(channel.push(1));  // full

    std::promise<bool> first;
    std::promise<bool> second;
    auto firstResult = first.get_future();
    auto secondResult = second.get_future();
    auto producer = [](Channel<int>& ch, int value, std::promise<bool>& out) -> Task<void> {
        while (!ch.push(value))
        {
            if (!co_await ch.waitWritable())
            {
                out.set_value(false);
                co_return;
            }
        }
        out.set_value(true);
    };

    bcos::task::wait(producer(channel, 2, first));
    bcos::task::wait(producer(channel, 3, second));
    BOOST_CHECK(firstResult.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);
    BOOST_CHECK(secondResult.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);

    // Each drain wakes the next FIFO producer. Neither producer is displaced by the other.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 1);
    BOOST_CHECK(firstResult.get());
    BOOST_CHECK(secondResult.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 2);
    BOOST_CHECK(secondResult.get());
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 3);
}

BOOST_AUTO_TEST_CASE(closeWakesAllParkedProducers)
{
    Channel<int> channel(inlinePoster(), 1);
    BOOST_CHECK(channel.push(1));  // full

    std::promise<bool> first;
    std::promise<bool> second;
    auto firstResult = first.get_future();
    auto secondResult = second.get_future();
    auto producer = [](Channel<int>& ch, std::promise<bool>& out) -> Task<void> {
        out.set_value(co_await ch.waitWritable());
    };
    bcos::task::wait(producer(channel, first));
    bcos::task::wait(producer(channel, second));

    channel.close();
    BOOST_CHECK(!firstResult.get());
    BOOST_REQUIRE(secondResult.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    BOOST_CHECK(!secondResult.get());
}

BOOST_AUTO_TEST_CASE(closeDeliversAllWakesWhenPosterThrows)
{
    for (bool deliverBeforeThrow : {false, true})
    {
        Channel<int> channel(
            [deliverBeforeThrow](std::function<void()> wake) {
                if (deliverBeforeThrow)
                {
                    wake();
                }
                throw std::runtime_error("poster failure");
            },
            1);
        BOOST_CHECK(channel.push(1));

        std::promise<bool> first;
        std::promise<bool> second;
        auto firstResult = first.get_future();
        auto secondResult = second.get_future();
        auto producer = [](Channel<int>& ch, std::promise<bool>& out) -> Task<void> {
            out.set_value(co_await ch.waitWritable());
        };
        bcos::task::wait(producer(channel, first));
        bcos::task::wait(producer(channel, second));

        BOOST_CHECK_NO_THROW(channel.close());
        BOOST_CHECK(!firstResult.get());
        BOOST_CHECK(!secondResult.get());
    }
}

BOOST_AUTO_TEST_CASE(destroyedParkedConsumerUnregistersBeforePush)
{
    Channel<int> channel(inlinePoster(), 16);
    {
        // A consumer frame destroyed while parked in recv() (a dropped pump task is teardown,
        // not misuse): the awaitable's destructor must unregister the waiter slot, so the
        // push below ENQUEUES instead of writing into or resuming the dead frame.
        auto pump = [](Channel<int>& ch) -> Task<void> { (void)co_await ch.recv(); }(channel);
        pump.start();  // runs synchronously up to the park in recv()
    }  // ~Task destroys the parked frame, awaitable included
    BOOST_CHECK(channel.push(42));
    // The value went to the queue rather than to the destroyed consumer.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 42);
}

BOOST_AUTO_TEST_CASE(destroyedParkedProducerUnregistersBeforePop)
{
    Channel<int> channel(inlinePoster(), 1);
    BOOST_CHECK(channel.push(1));  // full
    {
        auto producer = [](Channel<int>& ch) -> Task<void> { (void)co_await ch.waitWritable(); }(
                                                 channel);
        producer.start();  // parks: no space
    }  // frame destroyed while parked in waitWritable()
    // Draining must not write into or resume the dead producer frame.
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 1);
    BOOST_CHECK(channel.push(2));
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 2);
}

BOOST_AUTO_TEST_CASE(channelCanBeDestroyedWhileConsumerParked)
{
    auto channel = std::make_unique<Channel<int>>(inlinePoster(), 16);
    auto pump = [](Channel<int>& ch) -> Task<void> { (void)co_await ch.recv(); }(*channel);
    pump.start();                           // parked in recv()
    BOOST_CHECK_EQUAL(channel->size(), 0);  // nothing queued: the consumer is parked
    // The channel goes away first: the shared state is kept alive by the parked awaitable,
    // and destroying the parked frame afterwards unregisters without touching the channel.
    channel.reset();
}  // ~Task destroys the parked frame after the channel is gone — must not crash

BOOST_AUTO_TEST_CASE(displacedWaiterThrowingInlineLeavesNoDanglingSlot)
{
    Channel<int> channel(inlinePoster(), 16);
    std::promise<std::string> observed;

    // The displaced consumer does NOT catch: the displacement error escapes its await_resume,
    // its promise has no continuation and rethrows out of resume() — straight through the
    // inline poster and out of the SECOND recv's await_suspend, which per [expr.await]
    // rethrows the exception in the suspending coroutine. The second consumer therefore
    // observes the displaced waiter's exception at its own co_await.
    auto first = [](Channel<int>& ch) -> Task<void> { (void)co_await ch.recv(); }(channel);
    first.start();  // parked in recv()

    auto second = [](Channel<int>& ch, std::promise<std::string>& out) -> Task<void> {
        try
        {
            (void)co_await ch.recv();
            out.set_value("no exception");
        }
        catch (const std::exception& e)
        {
            out.set_value(e.what());
        }
    }(channel, observed);
    second.start();  // displaces the first; the inline wake throws back into this coroutine

    BOOST_CHECK_EQUAL(observed.get_future().get(), "channel waiter displaced by a second recv()");
    // Crucially the second recv() never completed its registration, so the channel stayed
    // consistent: a push lands in the queue and a fresh recv() picks it up.
    BOOST_CHECK(channel.push(7));
    BOOST_CHECK_EQUAL(syncWait(channel.recv()), 7);
}

BOOST_AUTO_TEST_SUITE_END()
