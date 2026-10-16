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
 * @brief Shared ASIOInterface test double for bcos-network's test suites.
 * @file TestAsioInterface.h
 *
 * The read path of bcos-network is a COMPILE-TIME policy (ASIOInterface::awaitableReadSome
 * takes the ReadPolicy as a template parameter; production is DefaultReadPolicy), so this fake
 * substitutes its own policy and parks every read completion in a FIFO the test controls:
 *
 *  - asynchronous mode (default): parks and every delivery are posted onto the fake's own
 *    single-threaded feed pool, so m_pendingReads is only ever touched on that one thread (the
 *    first read arm happens on the caller's thread — Session::startWithPolicy -> readLoop —
 *    hence the post). Packets fed with asyncAppendRecvPacket() complete parked reads with
 *    their buffered bytes.
 *  - synchronous mode (synchronousPark = true): the park happens inline on the calling thread
 *    and deliveries only happen when the test calls fireNextRead(), so a test can assert
 *    hasPendingRead() immediately after startWithPolicy<...>() and complete the read
 *    deterministically. Used by the session-lifetime tests; do not mix with stopReads().
 *
 * Migrated from the per-file fakes of bcos-gateway/test/unittests (FakeASIO / FakeASIO_FIB /
 * FakeASIO_Lifetime), unified for the library's own suites.
 */
#pragma once

#include "bcos-network/ASIOInterface.h"
#include "bcos-utilities/IOServicePool.h"
#include <boost/asio/error.hpp>
#include <atomic>
#include <list>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace bcos::network::test
{
class TestAsioInterface : public bcos::network::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    using ReadCompletion = task::detail::FireCompletion<boost::system::error_code, std::size_t>;

    explicit TestAsioInterface(std::size_t _poolSize = 1, std::string const& _name = "TestAsio",
        bool _synchronousPark = false)
      : bcos::network::ASIOInterface(
            std::make_shared<bcos::IOServicePool>(_poolSize, _name), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, _name + "-feed")),
        m_synchronousPark(_synchronousPark)
    {}
    // The base destructor is not virtual, but every owner of this fake holds a shared_ptr whose
    // control block remembers the concrete type, so deletion always runs this destructor.
    ~TestAsioInterface() noexcept = default;

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with startWithPolicy<TestAsioInterface::ReadPolicy>() so every read parks its
    // completion here instead of arming the real async_read_some.
    struct ReadPolicy
    {
        template <typename SocketT>
        static void invoke(bcos::network::ASIOInterface* asio,
            const std::shared_ptr<SocketT>& /*socket*/, ba::mutable_buffer buffers,
            ReadCompletion completion)
        {
            static_cast<TestAsioInterface*>(asio)->parkRead(buffers, std::move(completion));
        }
    };

    // Read-policy target (see ReadPolicy): park the completion and, in asynchronous mode, hand
    // the park over to the feed pool. The counter is incremented before the park is visible, so
    // a test can wait for a park (0 reads in flight means the loop is parked in waitWritable()
    // backpressure or dead — see the backpressure test).
    void parkRead(ba::mutable_buffer buffers, ReadCompletion completion)
    {
        ++m_readsInFlight;
        if (m_synchronousPark)
        {
            m_pendingReads.push_back(PendingRead{buffers, std::move(completion)});
            deliverIfPossible();
            return;
        }
        m_threadPool->post([this, buffers, completion = std::move(completion)]() mutable {
            m_pendingReads.push_back(PendingRead{buffers, std::move(completion)});
            deliverIfPossible();
        });
    }

    // Manually complete the oldest parked read. Synchronous-park harness only: firing resumes
    // the read loop SYNCHRONOUSLY on this thread, which may re-arm a fresh read (parking a new
    // completion) or unwind the loop entirely.
    void fireNextRead(boost::system::error_code error, std::size_t bytes)
    {
        if (m_pendingReads.empty())
        {
            return;
        }
        // Clear the slot BEFORE firing for the same reason the lifetime fake did: firing
        // re-arms the next read into a fresh slot, and a moved-from entry would otherwise be
        // destroyed only after that new completion was already parked in it.
        auto pending = std::move(m_pendingReads.front());
        m_pendingReads.pop_front();
        fireRead(std::move(pending.completion), error, bytes);
    }

    bool hasPendingRead() const { return !m_pendingReads.empty(); }

    // Test teardown: complete every parked read with operation_aborted so the read loops — and
    // the sessions their frames keep alive — unwind BEFORE the test nulls the socket or
    // destroys this fake. Poll readsInFlight() until 0 afterwards: the counter is decremented
    // only after the fired completion has synchronously unwound the whole read loop, so 0 means
    // the unwind is done and no coroutine touches the session any more.
    void stopReads()
    {
        m_threadPool->post([this] {
            while (!m_pendingReads.empty())
            {
                auto pending = std::move(m_pendingReads.front());
                m_pendingReads.pop_front();
                fireRead(
                    std::move(pending.completion), boost::asio::error::operation_aborted, 0);
            }
        });
    }
    std::size_t readsInFlight() const { return m_readsInFlight.load(); }

    void stop() { m_threadPool.reset(); }

    // Feed the wire: appendRecvPacket() is safe only before any read is armed (test setup);
    // asyncAppendRecvPacket() posts the append onto the feed pool, where deliveries happen.
    void appendRecvPacket(Packet packet) { m_recvPackets.push(std::move(packet)); }

    void asyncAppendRecvPacket(Packet packet)
    {
        m_threadPool->post([this, packet]() {
            m_recvPackets.push(std::move(packet));
            deliverIfPossible();
        });
    }

    // Synchronous helper for tests that exercise the fake itself (no session involved).
    template <typename Handler>
    void readSome(ba::mutable_buffer buffers, Handler&& handler)
    {
        handler(boost::system::error_code(), drainPackets(buffers));
    }

protected:
    std::size_t drainPackets(ba::mutable_buffer buffers)
    {
        std::size_t bytesTransferred = 0;
        auto limit = buffers.size();

        while (!m_recvPackets.empty())
        {
            auto packet = m_recvPackets.front();
            if (bytesTransferred + packet->size() > limit)
            {
                auto remaining = limit - bytesTransferred;
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet), remaining);
                bytesTransferred += remaining;
                packet->erase(packet->begin(), packet->begin() + remaining);
                break;
            }
            else
            {
                m_recvPackets.pop();
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet));
                buffers += packet->size();
                bytesTransferred += packet->size();
            }
        }
        return bytesTransferred;
    }

    // Everything below runs on the feed-pool thread only (asynchronous mode) or on the test
    // thread (synchronous mode): every read arm and every delivery are posted there, so
    // m_pendingReads is never touched from another thread. Multiple sessions may share this
    // fake, so parked reads form a FIFO list rather than a single slot.
    void deliverIfPossible()
    {
        if (m_pendingReads.empty() || m_recvPackets.empty())
        {
            return;
        }
        auto pending = std::move(m_pendingReads.front());
        m_pendingReads.pop_front();
        fireRead(std::move(pending.completion), boost::system::error_code(),
            drainPackets(pending.buffers));
    }

    // Fire a parked completion. The fire resumes the read loop SYNCHRONOUSLY on this thread —
    // on success the loop processes the messages and re-arms a fresh read (parking a new
    // completion and re-incrementing the counter), on error it unwinds completely — so the
    // counter is decremented only after the loop has settled.
    void fireRead(ReadCompletion completion, boost::system::error_code ec, std::size_t bytes)
    {
        completion(ec, bytes);
        --m_readsInFlight;
    }

    struct PendingRead
    {
        ba::mutable_buffer buffers;
        ReadCompletion completion;
    };

    std::queue<Packet> m_recvPackets;
    std::list<PendingRead> m_pendingReads;
    std::atomic<std::size_t> m_readsInFlight{0};
    bcos::IOServicePool::Ptr m_threadPool;
    bool m_synchronousPark{false};
};
}  // namespace bcos::network::test
