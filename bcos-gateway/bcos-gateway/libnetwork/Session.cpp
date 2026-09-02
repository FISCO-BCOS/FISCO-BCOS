
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libnetwork/SessionFace.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-gateway/libp2p/Common.h"  // for c_compressThreshold / c_zstdCompressLevel
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Overloaded.h"
#include "bcos-utilities/ZstdCompress.h"
#include <bcos-framework/protocol/Protocol.h>  // for MessageExtFieldFlag
#include <bcos-task/Wait.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iterator>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <utility>
#include <variant>

using namespace bcos;
using namespace bcos::gateway;

// ext field offset in the fixed base P2P header: length(4) | version(2) | packetType(2) |
// seq(4) | ext(2). ext is the last field of the base header (P2PMessage::MESSAGE_HEADER_LENGTH
// = 14); the V2 ttl/src/dst extension follows it.
constexpr size_t c_p2pHeaderExtOffset = 12;  // P2PMessage::MESSAGE_HEADER_LENGTH(14) - 2

Session::Session(
    std::shared_ptr<SocketFace> socket, Host& server, size_t _recvBufferSize, bool _forceSize)
  : m_maxRecvBufferSize(std::max<size_t>(_recvBufferSize, MIN_SESSION_RECV_BUFFER_SIZE)),
    // FIB-184: treat _recvBufferSize as the grow CEILING, not the initial allocation. Production
    // createSession passes the config-validated session_recv_buffer_size, which is forced to
    // 2 * allow_max_msg_size = 64MB; allocating that per session up front let authenticated TLS
    // connect/close churn exhaust the heap (SIGSEGV inside malloc during Session construction).
    // Allocate only INITIAL_SESSION_RECV_BUFFER_SIZE (16KB) initially and let the read loop grow
    // the buffer up to m_maxRecvBufferSize on demand, so only sessions that actually carry large
    // messages pay for a large buffer. _forceSize keeps the exact size for tests that assert a
    // specific small buffer.
    m_recvBuffer(_forceSize ? _recvBufferSize :
                              std::min<size_t>(_recvBufferSize, INITIAL_SESSION_RECV_BUFFER_SIZE)),
    m_server(server),
    m_socket(std::move(socket)),
    m_idleCheckTimer(
        std::make_shared<Timer>(m_socket->ioService(), m_idleTimeInterval, "idleChecker"))
{
    SESSION_LOG(INFO) << "[Session::Session] this=" << this
                      << LOG_KV("recvBufferSize", m_maxRecvBufferSize);
}

Session::~Session() noexcept
{
    SESSION_LOG(INFO) << "[Session::~Session] this=" << this;
    try
    {
        m_idleCheckTimer->stop();
        if (m_socket)
        {
            bi::tcp::socket& socket = m_socket->ref();
            if (m_socket->isConnected())
            {
                socket.close();
            }
        }
    }
    catch (...)
    {
        SESSION_LOG(ERROR) << "Deconstruct Session exception";
    }
}

NodeIPEndpoint Session::nodeIPEndpoint() const
{
    return m_socket->nodeIPEndpoint();
}

bool Session::active() const
{
    return active(m_server);
}

bool Session::active(Host& server) const
{
    return m_active && server.haveNetwork() && m_socket && m_socket->isConnected();
}

static void send(Session& session, ::ranges::input_range auto payloads,
    std::function<void(boost::system::error_code)> callback)
{
    if (!session.active() || !session.m_socket->isConnected())
    {
        // The zero-copy fast path's completion callback must still fire (once) so the suspended
        // coroutine (fastSendMessageWithoutResponse) is resumed with an error instead of leaking
        // the whole task::wait chain — which would pin the session/socket/service forever. Post to
        // the io thread rather than call inline: await_suspend has not returned yet, and a
        // synchronous resume would re-enter the awaiting coroutine from inside await_suspend.
        if (callback)
        {
            session.m_server.get().asioInterface()->post([callback = std::move(callback)]() {
                // the callback resumes a suspended coroutine whose await_resume may throw; keep
                // the exception out of io_context::run() (same containment as the write-
                // completion post)
                try
                {
                    callback(boost::asio::error::not_connected);
                }
                catch (std::exception const& e)
                {
                    SESSION_LOG(WARNING) << LOG_DESC("early-return write callback exception")
                                         << LOG_KV("what", boost::diagnostic_information(e));
                }
            });
        }
        return;
    }

    Payload payload{.m_data = Payload::MessageList{}, .m_callback = std::move(callback)};
    auto& vec = payload.m_data;
    if constexpr (::ranges::sized_range<decltype(payloads)>)
    {
        vec.reserve(::ranges::size(payloads));
    }
    for (const auto& data : payloads)
    {
        vec.emplace_back(data.data(), data.size());
    }

    session.m_writeQueue.push(std::move(payload));
    // FIB-185 (review): re-check the session state AFTER the push. drop() may have drained the
    // queue (CAS won) between the active() check above and this push, in which case this
    // payload's callback would never fire and the whole task::wait chain would leak (it pins the
    // session/socket/service forever). Either drop()'s drain already popped our payload (its
    // callback then fires with operation_aborted), or this re-check sees an inactive session and
    // drains it here. Both paths complete the callback exactly once through the same posted
    // channel used by the early-return branch above.
    if (!session.active())
    {
        Payload pending;
        while (session.m_writeQueue.try_pop(pending))
        {
            if (pending.m_callback)
            {
                session.m_server.get().asioInterface()->post(
                    [callback = std::move(pending.m_callback)]() {
                        // same containment as the early-return branch above: the callback
                        // resumes a coroutine whose await_resume may throw
                        try
                        {
                            callback(boost::asio::error::not_connected);
                        }
                        catch (std::exception const& e)
                        {
                            SESSION_LOG(WARNING)
                                << LOG_DESC("drained write callback exception")
                                << LOG_KV("what", boost::diagnostic_information(e));
                        }
                    });
            }
        }
        return;
    }
    session.write();
}

std::size_t Session::writeQueueSize()
{
    return static_cast<std::size_t>(!m_writeQueue.empty());
}

bool Session::tryPopSomeEncodedMsgs(
    std::vector<Payload>& encodedMsgs, size_t _maxSendDataSize)  // NOLINT
{
    // Desc: Try to send multi packets one time to improve the efficiency of sending data. Stop
    // batching once the configured byte budget (p2p.session_max_send_data_size) is exhausted;
    // the remainder stays queued for the next loop iteration. The message-count budget
    // (p2p.session_max_send_msg_count) is deliberately NOT enforced here — the base had that
    // condition commented out and drained the whole queue into a single scatter-gather write,
    // and re-enabling it capped every async_write at 10 messages, costing ceil(N/10) post +
    // async_write round-trips under broadcast fan-out. The byte budget alone bounds a single
    // write's memory footprint; see the behaviour-change note in the PR description.
    size_t totalDataSize = 0;
    Payload payload;
    // Floor the budget at 1 here — the point where the invariant "the byte budget is never 0"
    // is actually needed. The GatewayConfig clamp warns the operator, but Session::
    // setMaxSendDataSize stores whatever it is given, and a 0 budget makes the while below
    // never enter: tryPop returns false, writeLoop breaks on its first iteration and every
    // outbound write on the session stalls silently and permanently.
    size_t budget = std::max<size_t>(_maxSendDataSize, 1);
    while (totalDataSize < budget && m_writeQueue.try_pop(payload))
    {
        totalDataSize += payload.size();
        encodedMsgs.emplace_back(std::move(payload));
    }

    return totalDataSize > 0;
}

void Session::write()
{
    if (m_writingInFlight.exchange(true))
    {
        // a write loop is already in flight; it drains the queue
        return;
    }
    // Launch the loop on the socket's own io thread, NOT synchronously on the caller's stack.
    // write() is reached from a sender's await_suspend (fastSendMessageWith/WithoutResponse via
    // send()): running writeLoop's first iteration there would let a shutdown race (Host::stop()
    // clearing m_run between send()'s active() re-check and writeLoop's haveNetwork() check) call
    // drop(TCPError) on that stack, and drop()'s inline drain would invoke the just-queued
    // payload's callback — resuming the very coroutine whose await_suspend is still running
    // (undefined behaviour; await_resume's throw could even destroy the frame await_suspend is
    // standing on). With the launch posted, every settlement inside writeLoop — including the
    // drop() it may trigger and failBatch's inline branch — runs on an io thread, so no callback
    // can land on a sender's own stack. Cost is one post per write-loop start; the loop already
    // suspends into awaitableWrite, so steady-state cost is unchanged.
    //
    // Release-on-unwind, matching the old std::unique_lock(try_to_lock) writer flag: if the
    // launch itself throws (post allocation failure, or bad_weak_ptr from shared_from_this()),
    // the flag must not stay set, or every later write() would early-return at the CAS above and
    // the queue would never drain again. The exception is NOT propagated to the caller (see
    // above: the caller may be a sender's await_suspend with the payload already queued);
    // swallow, log and drop, which drains m_writeQueue and completes every queued callback.
    auto socket = m_socket;
    if (!socket)
    {
        // inactive session (setSocket(nullptr) raced the active() checks): the queue is
        // settled by send()'s post-push re-check or by drop(), so just release the flag
        m_writingInFlight.store(false);
        return;
    }
    try
    {
        boost::asio::post(socket->ioService(), [self = shared_from_this()]() {
            try
            {
                task::wait(self->writeLoop());
            }
            catch (...)
            {
                // the loop's own catches make this unreachable in practice; if the frame
                // allocation itself threw, release the flag and settle the queue
                self->m_writingInFlight.store(false);
                SESSION_LOG(ERROR) << LOG_DESC("write loop launch failed")
                                   << LOG_KV("what",
                                          boost::current_exception_diagnostic_information());
                self->drop(TCPError);
            }
        });
    }
    catch (...)
    {
        m_writingInFlight.store(false);
        SESSION_LOG(ERROR) << LOG_DESC("write loop launch failed")
                           << LOG_KV("what", boost::current_exception_diagnostic_information());
        drop(TCPError);
    }
}

task::Task<void> Session::writeLoop()
{
    // FIB-184: the coroutine frame holds a strong reference to the session for the whole write
    // loop. async_write operates on m_socket and reads from the batch buffers below; the frame
    // keeps the session (socket, SSL stream, batch buffers) alive until each write completes, so
    // a concurrent teardown on another thread cannot free them mid-write.
    auto self = shared_from_this();

    // Batch scratch owned by this frame for the whole loop (was the m_writings member; the frame
    // is the lifetime owner now, so no shared_ptr indirection is needed).
    std::vector<Payload> payloads;
    std::vector<boost::asio::const_buffer> buffers;

    // Frame-local RAII guard for the single-flight write flag. The completion-or-cancel rescue
    // (detail::AsioCompletion in AsioAwaitable.h) can DESTROY this frame without running the
    // loop body or its catch blocks (an armed write completion destroyed without invocation).
    // Frame destruction runs frame-local destructors only, so without this guard m_writingInFlight
    // — a Session member — would stay claimed forever (every later write() returns at the CAS and
    // the queue never drains again), and the batch already moved into `payloads` would be
    // destroyed with its m_callbacks never fired (the awaiting senders leak). Declared AFTER
    // payloads so the guard's destructor — which runs FIRST on frame destruction — still sees the
    // batch to fail. release() is called on every normal exit path (the loop tail), so the guard
    // fires only on the destroy path.
    struct WriteLoopGuard
    {
        Session* session;
        std::vector<Payload>& payloads;
        bool released = false;
        ~WriteLoopGuard()
        {
            if (released)
            {
                return;
            }
            session->m_writingInFlight.store(false);
            // fail the batch callbacks inline (not posted): on the destroy path there is no
            // guaranteed-live executor to post to, and the senders awaiting them are suspended
            // (their frames are alive), so a synchronous resume is safe here — the destroy path
            // never runs on a sender's own stack.
            for (auto& payload : payloads)
            {
                if (payload.m_callback)
                {
                    auto callback = std::move(payload.m_callback);
                    try
                    {
                        callback(boost::asio::error::operation_aborted);
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(WARNING)
                            << LOG_DESC("write batch callback failed on frame destroy")
                            << LOG_KV("what", boost::diagnostic_information(e));
                    }
                    catch (...)
                    {
                        // the guard's destructor is implicitly noexcept: a foreign exception
                        // (not derived from std::exception) escaping here would call
                        // std::terminate, so catch everything and log
                        SESSION_LOG(WARNING)
                            << LOG_DESC("write batch callback failed on frame destroy")
                            << LOG_KV("what", boost::current_exception_diagnostic_information());
                    }
                }
            }
            payloads.clear();
        }
        void release() { released = true; }
    } writeLoopGuard{this, payloads};

    // Fails the in-flight batch: the payloads have already been moved out of m_writeQueue and no
    // completion handler exists for them, so their callbacks would never fire (drop() only drains
    // m_writeQueue). Invoked from every exception exit so the batch is settled exactly once — a
    // batch destroyed with its callbacks unfired would pin every awaiting sender forever.
    // Posted rather than inline when the network is still up, for the same reason as drop()'s
    // drain: this code runs on the caller's stack, which may still be inside an await_suspend.
    auto failBatch = [this](std::vector<Payload>& batch) {
        for (auto& payload : batch)
        {
            if (payload.m_callback)
            {
                auto callback = std::move(payload.m_callback);
                if (m_server.get().haveNetwork())
                {
                    m_server.get().asioInterface()->post([callback = std::move(callback)]() {
                        // Same containment as the success-path write completion below: the
                        // callback resumes a waiter whose await_resume may throw, and this
                        // lambda runs inside io_context::run(), so nothing may escape it.
                        try
                        {
                            callback(boost::asio::error::operation_aborted);
                        }
                        catch (std::exception const& e2)
                        {
                            SESSION_LOG(WARNING)
                                << LOG_DESC("write callback exception")
                                << LOG_KV("what", boost::diagnostic_information(e2));
                        }
                    });
                }
                else
                {
                    try
                    {
                        callback(boost::asio::error::operation_aborted);
                    }
                    catch (std::exception const& e2)
                    {
                        SESSION_LOG(WARNING) << LOG_DESC("write callback exception")
                                             << LOG_KV("what", boost::diagnostic_information(e2));
                    }
                }
            }
        }
        batch.clear();
    };

    try
    {
        while (true)
        {
            if (!m_server.get().haveNetwork())
            {
                SESSION_LOG(WARNING) << "Host has gone";
                drop(TCPError);
                break;
            }
            if (!m_socket->isConnected())
            {
                SESSION_LOG(WARNING)
                    << "Error sending ssl socket is close!" << LOG_KV("endpoint", nodeIPEndpoint());
                drop(TCPError);
                break;
            }

            if (!tryPopSomeEncodedMsgs(payloads, m_maxSendDataSize))
            {
                // queue drained; fall through to the single exit below
                break;
            }

            auto outputIt = std::back_inserter(buffers);
            for (auto& payload : payloads)
            {
                payload.toConstBuffer(outputIt);
            }
            // `size` is unused: the loop re-derives the batch layout from `payloads` on each
            // iteration, and a short/failed write is handled through `error` alone
            [[maybe_unused]] auto [error, size] =
                co_await m_server.get().asioInterface()->awaitableWrite(
                    m_socket, std::move(buffers));

            buffers.clear();
            for (auto& payload : payloads)
            {
                if (payload.m_callback)
                {
                    m_server.get().asioInterface()->post(
                        [callback = std::move(payload.m_callback), error]() {
                            // The callback resumes a coroutine whose await_resume may throw
                            // (fastSendMessageWithoutResponse throws NetworkException on
                            // write failure). Catch so it cannot escape io_context::run().
                            try
                            {
                                callback(error);
                            }
                            catch (std::exception const& e)
                            {
                                SESSION_LOG(WARNING)
                                    << LOG_DESC("write callback exception")
                                    << LOG_KV("what", boost::diagnostic_information(e));
                            }
                        });
                }
            }
            payloads.clear();

            if (!active())
            {
                break;
            }
            m_lastWriteTime.store(utcSteadyTime());
            if (error)
            {
                SESSION_LOG(WARNING)
                    << LOG_DESC("onWrite error sending") << LOG_KV("message", error.message())
                    << LOG_KV("endpoint", nodeIPEndpoint());
                drop(TCPError);
                break;
            }
            // loop back and drain the next batch
        }
    }
    catch (std::exception& e)
    {
        SESSION_LOG(ERROR) << LOG_DESC("write error") << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::diagnostic_information(e));
        // FIB-185 (review): when the write path throws, the payloads have already been moved into
        // the local batch and no completion handler exists for them, so their callbacks would
        // never fire (drop() only drains m_writeQueue). Fail them here — posted rather than
        // inline, for the same reason as drop()'s drain: this catch runs on the caller's stack,
        // which may still be inside an await_suspend.
        failBatch(payloads);
        drop(TCPError);
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see AsioAwaitable.h);
        // fail the in-flight batch here too — the catch(std::exception&) arm above does it, and a
        // batch destroyed with its callbacks unfired would pin every awaiting sender forever
        SESSION_LOG(ERROR) << LOG_DESC("write error") << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::current_exception_diagnostic_information());
        failBatch(payloads);
        drop(TCPError);
    }

    // Single exit: clear the single-flight flag and, if the queue was refilled while this loop
    // was draining, hand the writer role to a fresh loop. Runs after every exit path (normal
    // drain, drop, exception). No wakeup is ever lost: a producer that pushed after the last
    // drain either wins the CAS in its own write() (flag already cleared) or this tail finds the
    // queue non-empty and re-arms. The re-arm (write()) posts the next loop's launch to the
    // socket's io thread, so it never extends this stack. Gated on active(): after a drop the
    // queue is drained by drop() (and late producers fail via send()'s re-check), so re-arming
    // there would just spin a fresh loop into the same teardown.
    //
    // The guard is released BEFORE the flag is cleared so its destructor never fires on this path
    // (it must not clear the flag that a re-armed write() has just claimed). The destroy path
    // (completion-or-cancel rescue) never reaches this tail, so the guard fires there instead.
    // The flag is cleared with exchange, not store: the tail never otherwise READS the flag, and
    // a plain store is release-only, so the queue re-check below would have no happens-before
    // edge from a producer's push (push, then exchange(true) observing the flag set). The seq_cst
    // exchange reads from the release sequence headed by that producer's exchange(true), giving
    // every such producer's push a happens-before edge to the re-check.
    writeLoopGuard.release();
    m_writingInFlight.exchange(false);
    if (active() && !m_writeQueue.empty())
    {
        try
        {
            write();
        }
        catch (std::exception const& e)
        {
            SESSION_LOG(WARNING) << LOG_DESC("write re-arm exception")
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }
    co_return;
}

void Session::drop(DisconnectReason _reason)
{
    // FIB-97-new: idempotency guard — only the first caller (wins the CAS) proceeds
    // with the actual teardown; all subsequent or concurrent calls are no-ops.
    bool expected = false;
    if (!m_dropped.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
    {
        return;
    }

    // Capture m_socket into a local shared_ptr BEFORE setting m_active = false.
    // The test thread may call setSocket(nullptr) as soon as active() returns false,
    // which creates a data race on m_socket.  Using a local copy guarantees we
    // hold a valid reference for the entire teardown sequence.
    auto socket = m_socket;

    m_active = false;

    // Fail any queued zero-copy sends: their completion callbacks resume suspended coroutine
    // chains (fastSendMessageWithoutResponse). Without this, a session that disappears before the
    // write completes would leak the whole task::wait chain — including the strong refs to the
    // session/socket/service it captured (a broadcast fan-out multiplies this by the peer count).
    // In-flight writes (the write loop's current batch) are covered by the write loop
    // (Session::writeLoop), which invokes their callbacks with the error when the socket close
    // cancels the write.
    // Also covers Session::write()'s early returns: they call drop(TCPError) right after the
    // payload has been pushed into m_writeQueue.
    //
    // FIB-185 (review): complete these callbacks OFF the caller's stack whenever an executor is
    // available. writeLoop never runs on a sender's stack anymore (Session::write() posts its
    // launch), but drop() remains reachable from arbitrary caller stacks — including write()'s
    // launch-failure catch, which can still sit inside a sender's await_suspend — so calling the
    // callback inline here could resume a coroutine from inside its own await_suspend (the exact
    // hazard send()'s early-return branch avoids by posting). Post to the shared pool; when the
    // host is already gone there is no live executor to hand it to, so fall back to inline —
    // the same shape as the notifyDisconnect / closeSocket branches below.
    Payload payload;
    while (m_writeQueue.try_pop(payload))
    {
        if (payload.m_callback)
        {
            if (m_server.get().haveNetwork())
            {
                m_server.get().asioInterface()->post([callback = std::move(payload.m_callback)]() {
                    // The callback resumes a coroutine whose await_resume may throw
                    // (fastSendMessageWithoutResponse throws NetworkException on write failure),
                    // and this lambda runs inside io_context::run() — contain it exactly as
                    // failBatch and the response-waiter flush below do.
                    try
                    {
                        callback(boost::asio::error::operation_aborted);
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(WARNING) << LOG_DESC("write callback exception during drop")
                                             << LOG_KV("what", boost::diagnostic_information(e));
                    }
                });
            }
            else
            {
                try
                {
                    payload.m_callback(boost::asio::error::operation_aborted);
                }
                catch (std::exception const& e)
                {
                    SESSION_LOG(WARNING) << LOG_DESC("write callback exception during drop")
                                         << LOG_KV("what", boost::diagnostic_information(e));
                }
            }
        }
    }

    // Fail this session's pending response waiters: a request/response send
    // (fastSendMessageWithResponse) suspends until a matching-seq reply arrives or its timeout
    // fires, and neither happens once the session is gone. The callback manager is shared
    // host-wide, so the flush is scoped to the seqs registered through THIS session
    // (m_pendingResponseSeqs) — popping the whole manager would spuriously fail every in-flight
    // request/response on every other session. Mirror the writeQueue drain above: fire
    // NetworkTimeout off the caller's stack when an executor is available, since drop() is
    // reachable from inside a sender's own await_suspend. Waiters registered after this flush
    // are caught by the active() re-check in the awaitable; the callback manager is injected by
    // the p2p layer and a bare Session (unit tests) has none.
    if (m_sessionCallbackManager)
    {
        std::vector<SessionResponseCallback::Ptr> pendingCallbacks;
        {
            std::lock_guard lock(x_pendingResponseSeqs);
            pendingCallbacks.reserve(m_pendingResponseSeqs.size());
            for (auto seq : m_pendingResponseSeqs)
            {
                if (auto handler = m_sessionCallbackManager->getCallback(seq, true))
                {
                    pendingCallbacks.emplace_back(std::move(handler));
                }
            }
            m_pendingResponseSeqs.clear();
        }
        for (auto& callback : pendingCallbacks)
        {
            if (!callback || !callback->callback)
            {
                continue;
            }
            if (callback->timeoutHandler)
            {
                callback->timeoutHandler->cancel();
            }
            if (m_server.get().haveNetwork())
            {
                m_server.get().asioInterface()->post([callback = std::move(callback)]() mutable {
                    // the callback resumes a coroutine whose await_resume may rethrow; keep
                    // the exception out of io_context::run() (same containment as onMessage /
                    // onTimeout / the write-completion post)
                    try
                    {
                        callback->callback(
                            NetworkException(P2PExceptionType::NetworkTimeout, "NetworkTimeout"),
                            Message::Ptr());
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(WARNING) << LOG_DESC("response callback exception during drop")
                                             << LOG_KV("what", boost::diagnostic_information(e));
                    }
                });
            }
            else
            {
                try
                {
                    callback->callback(
                        NetworkException(P2PExceptionType::NetworkTimeout, "NetworkTimeout"),
                        Message::Ptr());
                }
                catch (std::exception const& e)
                {
                    SESSION_LOG(WARNING) << LOG_DESC("response callback exception during drop")
                                         << LOG_KV("what", boost::diagnostic_information(e));
                }
            }
        }
    }

    int errorCode = P2PExceptionType::Disconnect;
    std::string errorMsg = "Disconnect";
    if (_reason == DuplicatePeer)
    {
        errorCode = P2PExceptionType::DuplicateSession;
        errorMsg = "DuplicateSession";
    }

    // Guard against null socket (e.g. test sets it to nullptr before destructor)
    if (!socket)
    {
        return;
    }

    SESSION_LOG(INFO) << "drop, call and erase all callback in this session!"
                      << LOG_KV("this", this) << LOG_KV("endpoint", socket->nodeIPEndpoint());

    if (m_messageHandler)
    {
        // FIB-186 (vector D): run the teardown notification on the dedicated teardown executor, NOT
        // the shared IOServicePool. This handler drives Service::onMessage's error path ->
        // onDisconnect -> onRemoveNodeIDs -> syncLatestNodeIDList; running it on the same reactor
        // that carries inbound message delivery let a persistent bulk-disconnect flood starve
        // inter-validator message delivery and permanently halt consensus. postTeardown keeps it
        // off the delivery reactor. Ordering vs message delivery is unchanged: onDisconnect already
        // ran asynchronously and unordered relative to delivery.
        auto notifyDisconnect = [self = weak_from_this(), errorCode,
                                    errorMsg = std::move(errorMsg)]() {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            session->m_messageHandler(
                NetworkException(errorCode, errorMsg), session, Message::Ptr());
        };
        // Once haveNetwork() is false the Host is on its way out, so run the notification inline
        // rather than handing it to an executor whose remaining lifetime we do not control here.
        //
        // NOTE, because the surrounding machinery changed under this fix: the teardown executor is
        // deliberately NOT stopped by Host::stop() (see Host.cpp) -- it lives until ~Host -- and
        // Host::stop() no longer stops the ASIO interface either, so the shared pool's threads are
        // still running at this point. In practice this branch is currently unreachable during
        // shutdown: Service::stop() calls Host::stop() first, which makes Session::active() false,
        // and P2PSession::stop() only calls disconnect() on an active session. The reachable
        // shutdown-time callers are socket error paths, which already run on the socket's own
        // io_context.
        //
        // KNOWN HAZARD of this inline branch (recorded, not a reason to keep it): any caller that
        // reaches drop() while holding x_sessions would self-deadlock here, because the inline
        // notifyDisconnect re-enters Service::onDisconnect -> getP2PSessionByNodeId, which takes
        // that same non-reentrant shared_mutex -- and Service::stop() does iterate sessions under
        // it exclusively. No such caller exists today (see the reachability note above). Making
        // this branch unconditionally async would remove the hazard outright, since the teardown
        // thread never holds x_sessions; that is worth doing on its own, with the shutdown-path
        // verification it deserves, rather than inside a merge.
        if (m_server.get().haveNetwork())
        {
            m_server.get().postTeardown(std::move(notifyDisconnect));
        }
        else
        {
            notifyDisconnect();
        }
    }

    // FIB-184: serialize the SSL/socket teardown onto the socket's own (single-threaded)
    // io_context. drop() can be invoked from another thread (Service-layer teardown, duplicate-peer
    // handling) while an async_read_some/async_write is still in flight on the socket's io_context
    // thread. Running close()/async_shutdown inline on the caller thread would then touch the same
    // ssl::stream concurrently with those handlers. Posting the teardown to the socket's io_context
    // makes it run on the same single thread that services every read/write for this session —
    // i.e. a per-session strand — so socket operations never overlap. The strong self capture keeps
    // the session (and its socket) alive until the teardown runs.
    //
    // Shutdown path exception: Service::stop() calls Host::stop() BEFORE dropping sessions, and the
    // shared IOServicePool is torn down shortly after by whoever owns it, so once the network is
    // down a posted handler may never run — the socket would never be closed and the posted task
    // would pin this session in a dead io_context queue. Close inline instead, matching the old
    // synchronous teardown behaviour on shutdown.
    //
    // Do not read this as "the io_context threads are already joined": Host::stop() only clears
    // m_run now (it no longer stops the ASIO interface), so the shared pool is still live here.
    // What makes the inline close safe is the caller, not a quiesced pool — see the note on the
    // teardown-notification branch above.
    if (m_server.get().haveNetwork())
    {
        boost::asio::post(socket->ioService(),
            [self = shared_from_this(), _reason]() { self->closeSocket(_reason); });
    }
    else
    {
        closeSocket(_reason);
    }
}

void Session::closeSocket(DisconnectReason _reason)
{
    // Take a local strong reference before touching the socket, for the same reason drop() does:
    // a concurrent setSocket(nullptr) must not turn this into a null dereference mid-teardown.
    auto socket = m_socket;
    if (!socket || !socket->isConnected())
    {
        return;
    }
    try
    {
        if (_reason == DisconnectRequested || _reason == DuplicatePeer || _reason == ClientQuit ||
            _reason == UserReason)
        {
            SESSION_LOG(DEBUG) << "[drop] closing remote " << socket->remoteEndpoint()
                               << LOG_KV("reason", reasonOf(_reason))
                               << LOG_KV("endpoint", socket->nodeIPEndpoint());
        }
        else
        {
            SESSION_LOG(INFO) << "[drop] closing remote " << socket->remoteEndpoint()
                              << LOG_KV("reason", reasonOf(_reason))
                              << LOG_KV("endpoint", socket->nodeIPEndpoint());
        }

        /// if get Host object failed, close the socket directly
        if (socket->isConnected())
        {
            socket->close();
        }
        auto shutdown_timer = std::make_shared<boost::asio::steady_timer>(
            socket->ioService(), std::chrono::milliseconds(m_shutDownTimeThres));
        /// async wait for shutdown
        shutdown_timer->async_wait([socket](const boost::system::error_code& error) {
            /// drop operation has been aborted
            if (error == boost::asio::error::operation_aborted)
            {
                SESSION_LOG(DEBUG)
                    << "[drop] operation aborted  by async_shutdown"
                    << LOG_KV("value", error.value()) << LOG_KV("message", error.message());
                return;
            }
            /// shutdown timer error
            if (error && error != boost::asio::error::operation_aborted)
            {
                SESSION_LOG(WARNING)
                    << "[drop] shutdown timer failed" << LOG_KV("failedValue", error.value())
                    << LOG_KV("message", error.message());
            }
            /// force to shutdown when timeout
            if (socket->ref().is_open())
            {
                SESSION_LOG(WARNING) << "[drop] timeout, force close the socket"
                                     << LOG_KV("remote endpoint", socket->nodeIPEndpoint());
                socket->close();
            }
        });

        /// async shutdown normally
        socket->sslref().async_shutdown(
            [socket, shutdown_timer](const boost::system::error_code& error) {
                shutdown_timer->cancel();
                if (error)
                {
                    SESSION_LOG(INFO)
                        << "[drop] shutdown failed " << LOG_KV("failedValue", error.value())
                        << LOG_KV("message", error.message());
                }
                /// force to close the socket
                if (socket->ref().is_open())
                {
                    SESSION_LOG(WARNING) << LOG_DESC("force to shutdown session")
                                         << LOG_KV("endpoint", socket->nodeIPEndpoint());
                    socket->close();
                }
            });
    }
    catch (...)
    {
        SESSION_LOG(ERROR) << LOG_DESC("drop error")
                           << LOG_KV("endpoint", socket->nodeIPEndpoint());
    }
}

void Session::disconnect(DisconnectReason _reason)
{
    drop(_reason);
}

void Session::start()
{
    // Production read policy (default): the read loop compiles against
    // ASIOInterface::DefaultReadPolicy — a direct async_read_some call. Read-loop test fakes
    // that want to park reads call the templated startWithPolicy<FakePolicy>() instead (see
    // SessionReadLoop.h).
    startWithPolicy<ASIOInterface::DefaultReadPolicy>();
}

bool Session::checkRead(boost::system::error_code _ec)
{
    if (_ec && _ec.category() != boost::asio::error::get_misc_category() &&
        _ec.value() != boost::asio::error::eof)
    {
        SESSION_LOG(WARNING) << LOG_DESC("checkRead error") << LOG_KV("message", _ec.message());
        drop(TCPError);

        return false;
    }

    return true;
}


void Session::onMessage(NetworkException const& e, Message::Ptr message)
{
    m_server.get().asioInterface()->post(
        [self = weak_from_this(), e, message = std::move(message)]() {
            try
            {
                auto session = self.lock();
                if (!session)
                {
                    return;
                }
                // TODO: move the logic to Service for deal with the forwarding message
                if (!message->dstP2PNodeID().empty() &&
                    message->dstP2PNodeID() != session->m_hostInfo.p2pID &&
                    message->dstP2PNodeID() != session->m_hostInfo.rawP2pID)
                {
                    session->m_messageHandler(e, session, message);
                    return;
                }
                // in-activate session
                if (!session->m_active || !session->m_server.get().haveNetwork())
                {
                    return;
                }

                if (!message->isRespPacket())
                {
                    session->m_messageHandler(e, session, message);
                    return;
                }

                auto callbackManager = session->sessionCallbackManager();
                auto callbackPtr = callbackManager->getCallback(message->seq(), true);
                // without callback, call default handler
                if (!callbackPtr)
                {
                    SESSION_LOG(WARNING)
                        << LOG_BADGE("onMessage")
                        << LOG_DESC("callback not found, maybe the callback timeout")
                        << LOG_KV("endpoint", session->nodeIPEndpoint())
                        << LOG_KV("seq", message->seq()) << LOG_KV("resp", message->isRespPacket());
                    return;
                }
                // erase on the session that REGISTERED the seq: the callback manager is
                // host-shared, so a routed response can arrive on a different session than the
                // request went out on — erasing here would miss the owner's bookkeeping
                if (auto owner = callbackPtr->owner.lock())
                {
                    owner->removePendingResponseSeq(message->seq());
                }

                // with callback
                if (callbackPtr->timeoutHandler)
                {
                    callbackPtr->timeoutHandler->cancel();
                }
                auto& callback = callbackPtr->callback;
                if (!callback)
                {
                    return;
                }
                callback(e, message);
            }
            catch (std::exception const& e)
            {
                SESSION_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_DESC("onMessage exception")
                                     << LOG_KV("msg", boost::diagnostic_information(e));
            }
        });
}

void Session::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    if (error)
    {
        // SESSION_LOG(TRACE) << "timer cancel" << error;
        return;
    }

    ResponseCallback::Ptr callback = m_sessionCallbackManager->getCallback(seq, true);
    if (!callback)
    {
        return;
    }
    removePendingResponseSeq(seq);
    NetworkException e(P2PExceptionType::NetworkTimeout, "NetworkTimeout");
    callback->callback(e, Message::Ptr());
}

void Session::checkNetworkStatus()
{
    m_idleCheckTimer->restart();
    try
    {
        auto now = utcSteadyTime();
        // read idle
        if ((m_lastReadTime + m_idleTimeInterval) < now)
        {
            SESSION_LOG(WARNING) << LOG_DESC(
                                        "Long time without read operation, maybe session "
                                        "inactivated, drop the session")
                                 << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
            return;
        }
        // write idle
        if ((m_lastWriteTime + m_idleTimeInterval) < now)
        {
            SESSION_LOG(WARNING) << LOG_DESC(
                                        "Long time without write operation, maybe session "
                                        "inactivated, drop the session")
                                 << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
            return;
        }
    }
    catch (std::exception const& e)
    {
        SESSION_LOG(WARNING) << LOG_DESC("checkNetworkStatus error")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}

template <typename View>
task::Task<Message::Ptr> fastSendMessageWithResponse(
    Session& session, const Message& message, View& view, Options& options)
{
    struct Awaitable
    {
        std::reference_wrapper<Options> m_options;
        std::reference_wrapper<Host> m_host;
        std::reference_wrapper<const Message> m_message;
        std::weak_ptr<Session> m_self;
        std::reference_wrapper<SessionCallbackManagerInterface> m_sessionCallbackManager;
        std::reference_wrapper<View> m_view;
        std::variant<NetworkException, Message::Ptr> m_result;

        // Resume gate: the zero-copy write enqueues raw views into this coroutine frame's buffers
        // (header/payload), so the frame may only unwind once the write has completed. The
        // coroutine is therefore resumed only when BOTH the write completion has fired AND a
        // terminal event (response / timeout / teardown flush) has fired; resuming on the event
        // alone would let a timeout or drop() destroy the frame while the write queue or an
        // in-flight async_write still references it (use-after-free on the stalled-peer path).
        struct ResumeGate
        {
            std::mutex mutex;
            bool writeDone = false;
            bool eventFired = false;
            std::coroutine_handle<> handle;
        };
        std::shared_ptr<ResumeGate> m_gate = std::make_shared<ResumeGate>();

        constexpr static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            auto session = m_self.lock();
            auto gate = m_gate;
            gate->handle = handle;
            auto seq = m_message.get().seq();

            auto handler = std::make_shared<ResponseCallback>();
            handler->callback = [this, gate](NetworkException exception, Message::Ptr response) {
                if (exception.errorCode() != 0)
                {
                    m_result.emplace<NetworkException>(std::move(exception));
                }
                else
                {
                    m_result.emplace<Message::Ptr>(std::move(response));
                }
                std::coroutine_handle<> toResume;
                {
                    std::lock_guard lock(gate->mutex);
                    gate->eventFired = true;
                    if (gate->writeDone)
                    {
                        toResume = gate->handle;
                    }
                }
                if (toResume)
                {
                    // resume last: the coroutine frame (this awaitable included) dies inside
                    toResume.resume();
                }
            };
            if (m_options.get().timeout > 0)
            {
                handler->timeoutHandler.emplace(
                    m_host.get().asioInterface()->newTimer(m_options.get().timeout));
                handler->timeoutHandler->async_wait([self = m_self, seq](
                                                        const boost::system::error_code& _error) {
                    try
                    {
                        if (auto session = self.lock())
                        {
                            session->onTimeout(_error, seq);
                        }
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(WARNING) << LOG_DESC("async_wait exception")
                                             << LOG_KV("message", boost::diagnostic_information(e));
                    }
                });
                handler->startTime = utcSteadyTime();
            }
            handler->owner = m_self;
            m_sessionCallbackManager.get().addCallback(seq, std::move(handler));
            session->addPendingResponseSeq(seq);
            // Teardown race: drop()'s flush may already have run before this callback was
            // registered (drop is CAS single-shot, so it will not flush again). Fail the waiter
            // fast instead of leaving it to the response timer.
            if (!session->active())
            {
                session->removePendingResponseSeq(seq);
                auto claimed = m_sessionCallbackManager.get().getCallback(seq, true);
                if (claimed)
                {
                    // we still own the callback: complete inline — returning false means the
                    // coroutine is never suspended and await_resume runs synchronously
                    if (claimed->timeoutHandler)
                    {
                        claimed->timeoutHandler->cancel();
                    }
                    m_result.emplace<NetworkException>(
                        NetworkException(P2PExceptionType::NetworkTimeout, "session dropped"));
                    return false;
                }
                // the flush claimed the callback; no write will happen, so mark the write side
                // done. The flush's event may already have fired (its posted lambda can run
                // before this lock is taken): then m_result already holds its NetworkException
                // (written before eventFired under this same mutex, hence visible here) —
                // complete synchronously instead of suspending with no resume source left.
                std::lock_guard lock(gate->mutex);
                gate->writeDone = true;
                return !gate->eventFired;
            }

            ::send(*session, ::ranges::views::all(m_view.get()),
                // capture the manager by reference up front: after writeDone is stored, a
                // concurrent event may resume and destroy this frame before the claim below
                // runs, so the claim path must not load through `this` (the Host-owned manager
                // outlives every session). Frame access is safe again once the claim is won —
                // winning the manager pop excludes every event channel.
                [this, gate, seq, &manager = m_sessionCallbackManager.get()](
                    boost::system::error_code errorCode) {
                    std::coroutine_handle<> toResume;
                    bool claimOnWriteError = false;
                    {
                        std::lock_guard lock(gate->mutex);
                        gate->writeDone = true;
                        if (gate->eventFired)
                        {
                            toResume = gate->handle;
                        }
                        else if (errorCode.failed())
                        {
                            claimOnWriteError = true;
                        }
                    }
                    if (claimOnWriteError)
                    {
                        // The write failed before any response/timeout/flush: no response can
                        // arrive, so claim the callback back (cancelling its timer) and fail the
                        // waiter now — otherwise the registered handler (which points into this
                        // frame) would dangle after the frame unwinds.
                        auto claimed = manager.getCallback(seq, true);
                        if (claimed)
                        {
                            if (auto session = m_self.lock())
                            {
                                session->removePendingResponseSeq(seq);
                            }
                            if (claimed->timeoutHandler)
                            {
                                claimed->timeoutHandler->cancel();
                            }
                            m_result.emplace<NetworkException>(
                                NetworkException(errorCode.value(), errorCode.message()));
                            // wrap the resume: an exception escaping the resumed coroutine
                            // (a with-response caller that does not catch) must not unwind
                            // this asio handler — same containment as the drop flush
                            try
                            {
                                gate->handle.resume();
                            }
                            catch (std::exception const& e)
                            {
                                SESSION_LOG(WARNING)
                                    << LOG_DESC("write-error resume exception")
                                    << LOG_KV("what", boost::diagnostic_information(e));
                            }
                        }
                        // else: the event fired concurrently and resumes via the gate
                    }
                    else if (toResume)
                    {
                        // resume last: the coroutine frame (this awaitable included) dies inside;
                        // wrapped for the same reason as above
                        try
                        {
                            toResume.resume();
                        }
                        catch (std::exception const& e)
                        {
                            SESSION_LOG(WARNING)
                                << LOG_DESC("write-complete resume exception")
                                << LOG_KV("what", boost::diagnostic_information(e));
                        }
                    }
                });
            return true;
        }
        Message::Ptr await_resume()
        {
            return std::visit(
                bcos::overloaded(
                    [](NetworkException& exception) -> Message::Ptr {
                        BOOST_THROW_EXCEPTION(exception);
                        return {};
                    },
                    [](Message::Ptr& response) -> Message::Ptr { return std::move(response); }),
                m_result);
        }
    } awaitable{.m_options = options,
        .m_host = session.m_server,
        .m_message = message,
        .m_self = session.shared_from_this(),
        .m_sessionCallbackManager = *session.m_sessionCallbackManager,
        .m_view = view,
        .m_result = {}};

    co_return co_await awaitable;
}

template <typename View>
task::Task<void> fastSendMessageWithoutResponse(Session& session, View view)
{
    struct Awaitable
    {
        std::reference_wrapper<Session> m_self;
        std::reference_wrapper<View> m_view;
        NetworkException m_exception;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            ::send(m_self, ::ranges::views::all(m_view.get()),
                [this, handle](boost::system::error_code errorCode) {
                    if (errorCode.failed())
                    {
                        m_exception = NetworkException(errorCode.value(), errorCode.message());
                    }
                    handle.resume();
                });
        }
        void await_resume()
        {
            if (m_exception.errorCode() != 0)
            {
                BOOST_THROW_EXCEPTION(m_exception);
            }
        }
    } awaitable{session, view, {}};
    co_await awaitable;
}

bcos::task::Task<Message::Ptr> bcos::gateway::Session::fastSendMessage(
    const Message& message, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    if (!active())
    {
        SESSION_LOG(WARNING) << "Session inactive";
        co_return {};
    }

    // Materialize the payload views once: the incoming any_view is category::input (single-pass),
    // while the compression/join and the length passes below each iterate the payloads. Copying
    // the (cheap) views into a forward container makes every later pass multi-pass safe without
    // changing the interface contract.
    boost::container::small_vector<bytesConstRef, 3> payloadRefs;
    for (auto const& ref : payloads)
    {
        payloadRefs.push_back(ref);
    }

    // Encode the base header once. When compression (below) applies, the COMPRESS flag is stamped
    // directly onto the encoded header's ext field — the caller's message is left untouched (it is
    // const), so a reused message object (broadcast fan-out / retry loop) can never leak the flag
    // to a peer that receives an uncompressed frame.
    bytes headerBuffer;
    if (!message.encodeHeader(headerBuffer))
    {
        // e.g. P2PMessageOptions::encode failed (empty/oversized src/dst IDs). Sending a frame
        // whose header claims "has options" while the options are missing would make the peer
        // drop the connection instead of the message.
        BOOST_THROW_EXCEPTION(NetworkException(-1, "encode header failed"));
    }
    // encodeHeader always emits the fixed 14-byte base header (options are appended after it, and
    // option-encoding failures above are already rejected); guard the two fixed-offset writes
    // below defensively anyway.
    if (headerBuffer.size() < c_p2pHeaderExtOffset + sizeof(uint16_t))
    {
        BOOST_THROW_EXCEPTION(NetworkException(-1, "encode header failed: header too short"));
    }

    // Zero-copy send by default. When compression is enabled and the payload is large enough (and
    // the wire format is V2+ — the same rule as the removed asyncSendMessage path, P2PMessage::
    // tryToCompressPayload also requires V2), the payload views are joined into a frame-owned
    // buffer and compressed; the wire then carries header + the compressed payload. Both buffers
    // live in this coroutine frame for the whole (deferred) send.
    bcos::bytes joinedPayload;
    bcos::bytes compressedPayload;
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> wirePayloads =
        ::ranges::views::all(payloadRefs);
    if (m_enableCompress && message.version() >= (uint16_t)bcos::protocol::ProtocolVersion::V2)
    {
        uint32_t payloadSize = 0;
        for (auto const& ref : payloadRefs)
        {
            payloadSize += ref.size();
        }
        if (payloadSize > c_compressThreshold)
        {
            joinedPayload.reserve(payloadSize);
            for (auto const& ref : payloadRefs)
            {
                joinedPayload.insert(joinedPayload.end(), ref.begin(), ref.end());
            }
            if (ZstdCompress::compress(
                    ref(joinedPayload), compressedPayload, (int)c_zstdCompressLevel))
            {
                // stamp the COMPRESS flag onto the encoded header only (ext field of the fixed
                // base header), before the frame is written to the wire
                const auto compressedExt = static_cast<uint16_t>(
                    message.ext() | bcos::protocol::MessageExtFieldFlag::COMPRESS);
                *(uint16_t*)(headerBuffer.data() + c_p2pHeaderExtOffset) =
                    boost::asio::detail::socket_ops::host_to_network_short(compressedExt);
                wirePayloads = ::ranges::views::single(bcos::ref(std::as_const(compressedPayload)));
            }
            else
            {
                wirePayloads = ::ranges::views::single(bcos::ref(std::as_const(joinedPayload)));
            }
        }
    }

    auto view = ::ranges::views::concat(
        ::ranges::views::single(bcos::ref(std::as_const(headerBuffer))), std::move(wirePayloads));
    uint32_t totalLength = 0;
    for (auto ref : view)
    {
        totalLength += ref.size();
    }
    *(uint32_t*)headerBuffer.data() =
        boost::asio::detail::socket_ops::host_to_network_long(totalLength);

    // The pre-send checks run in the same order as the removed asyncSendMessage path (active →
    // allowMaxMsgSize → beforeMessageHandler), but the size limit is judged on the COMPRESSED wire
    // bytes (totalLength, computed above after compression) rather than the pre-compression
    // estimate the old path used: the peer's read side (the read loop) rejects frames by the
    // on-the-wire length too, so a message that only fits after compression is accepted here and
    // decodes on the peer — this is an intentional, documented divergence from asyncSendMessage.
    // The outgoing rate-limit handler receives the ACTUAL wire bytes (totalLength including the
    // payload views) explicitly — a zero-copy message does not carry its payload, so
    // message.length() alone would under-count the outgoing traffic.
    if (totalLength > allowMaxMsgSize())
    {
        SESSION_LOG(WARNING) << LOG_BADGE("fastSendMessage") << LOG_DESC("msg size overflow")
                             << LOG_KV("msgSize", totalLength)
                             << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());
        BOOST_THROW_EXCEPTION(NetworkException(-1, "Msg size overflow"));
    }

    // The fast path must honour the same pre-send (outgoing rate-limit) check that the callback
    // path (asyncSendMessage) enforces; otherwise routing sends through fastSendMessage would
    // silently bypass outgoing bandwidth/QPS limiting. A rejection surfaces as a thrown
    // NetworkException (e.g. OutBWOverflow / InQPSOverflow) so coroutine retry loops can stop.
    if (auto result =
            (m_beforeMessageHandler ? m_beforeMessageHandler(*this, message, totalLength) :
                                      std::nullopt))
    {
        const auto& error = result.value();
        BOOST_THROW_EXCEPTION(NetworkException((int64_t)error.errorCode(), error.errorMessage()));
    }

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        // Log the ext actually on the wire (the header's ext field), not the caller's ext: when
        // compression applied, the wire header carries the COMPRESS bit that message.ext() does
        // not have — a future "peer failed to decompress" investigation must read the wire value.
        const uint16_t wireExt = boost::asio::detail::socket_ops::network_to_host_short(
            *reinterpret_cast<uint16_t const*>(headerBuffer.data() + c_p2pHeaderExtOffset));
        SESSION_LOG(TRACE) << LOG_DESC("Session fastSendMessage")
                           << LOG_KV("endpoint", nodeIPEndpoint()) << LOG_KV("seq", message.seq())
                           << LOG_KV("packetType", message.packetType()) << LOG_KV("ext", wireExt)
                           << LOG_KV("this", this);
    }
    if (options.response)
    {
        co_return co_await fastSendMessageWithResponse(*this, message, view, options);
    }
    else
    {
        co_await fastSendMessageWithoutResponse(*this, std::move(view));
        co_return {};
    }
}

size_t bcos::gateway::Payload::size() const
{
    return ::ranges::accumulate(
        m_data, size_t(0), [](size_t sum, const bytesConstRef& ref) { return sum + ref.size(); });
}
std::size_t bcos::gateway::SessionRecvBuffer::readPos() const
{
    return m_readPos;
}
std::size_t bcos::gateway::SessionRecvBuffer::writePos() const
{
    return m_writePos;
}
std::size_t bcos::gateway::SessionRecvBuffer::dataSize() const
{
    return m_writePos - m_readPos;
}
size_t bcos::gateway::SessionRecvBuffer::recvBufferSize() const
{
    return m_recvBufferSize;
}
bool bcos::gateway::SessionRecvBuffer::onRead(std::size_t _dataSize)
{
    if (m_readPos + _dataSize <= m_writePos)
    {
        m_readPos += _dataSize;
        return true;
    }
    return false;
}
bool bcos::gateway::SessionRecvBuffer::onWrite(std::size_t _dataSize)
{
    if (m_writePos + _dataSize <= m_recvBufferSize)
    {
        m_writePos += _dataSize;
        return true;
    }
    return false;
}
bool bcos::gateway::SessionRecvBuffer::resizeBuffer(size_t _bufferSize)
{
    if (_bufferSize > m_recvBufferSize)
    {
        m_recvBuffer.resize(_bufferSize);
        m_recvBufferSize = _bufferSize;

        return true;
    }

    return false;
}
void bcos::gateway::SessionRecvBuffer::moveToHeader()
{
    if (m_writePos > m_readPos)
    {
        memmove(m_recvBuffer.data(), m_recvBuffer.data() + m_readPos, m_writePos - m_readPos);
        m_writePos -= m_readPos;
        m_readPos = 0;
    }
    else if (m_writePos == m_readPos)
    {
        m_readPos = 0;
        m_writePos = 0;
    }
}
bcos::bytesConstRef bcos::gateway::SessionRecvBuffer::asReadBuffer() const
{
    return {m_recvBuffer.data() + m_readPos, m_writePos - m_readPos};
}
bcos::bytesConstRef bcos::gateway::SessionRecvBuffer::asWriteBuffer() const
{
    return {m_recvBuffer.data() + m_writePos, m_recvBufferSize - m_writePos};
}
bcos::gateway::Host& bcos::gateway::Session::host()
{
    return m_server;
}
std::shared_ptr<SocketFace> bcos::gateway::Session::socket()
{
    return m_socket;
}
void bcos::gateway::Session::setSocket(const std::shared_ptr<SocketFace>& socket)
{
    m_socket = socket;
}
bcos::gateway::MessageFactory::Ptr bcos::gateway::Session::messageFactory() const
{
    return m_messageFactory;
}
void bcos::gateway::Session::setMessageFactory(const MessageFactory::Ptr& _messageFactory)
{
    m_messageFactory = _messageFactory;
}
bcos::gateway::SessionCallbackManagerInterface::Ptr bcos::gateway::Session::sessionCallbackManager()
    const
{
    return m_sessionCallbackManager;
}
void bcos::gateway::Session::setSessionCallbackManager(
    const SessionCallbackManagerInterface::Ptr& _sessionCallbackManager)
{
    m_sessionCallbackManager = _sessionCallbackManager;
}
const std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)>&
bcos::gateway::Session::messageHandler()
{
    return m_messageHandler;
}
void bcos::gateway::Session::setMessageHandler(
    std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> messageHandler)

{
    m_messageHandler = std::move(messageHandler);
}
void bcos::gateway::Session::setBeforeMessageHandler(
    std::function<std::optional<bcos::Error>(SessionFace&, const Message&, uint32_t)> handler)
{
    m_beforeMessageHandler = std::move(handler);
}
void bcos::gateway::Session::setHostInfo(P2PInfo _hostInfo)
{
    m_hostInfo = std::move(_hostInfo);
}
uint32_t bcos::gateway::Session::maxReadDataSize() const
{
    return m_maxReadDataSize;
}
void bcos::gateway::Session::setMaxReadDataSize(uint32_t _maxReadDataSize)
{
    m_maxReadDataSize = _maxReadDataSize;
}
uint32_t bcos::gateway::Session::maxSendDataSize() const
{
    return m_maxSendDataSize;
}
void bcos::gateway::Session::setMaxSendDataSize(uint32_t _maxSendDataSize)
{
    m_maxSendDataSize = _maxSendDataSize;
}
uint32_t bcos::gateway::Session::allowMaxMsgSize() const
{
    return m_allowMaxMsgSize;
}
void bcos::gateway::Session::setAllowMaxMsgSize(uint32_t _allowMaxMsgSize)
{
    m_allowMaxMsgSize = _allowMaxMsgSize;
}
void bcos::gateway::Session::setEnableCompress(bool _enableCompress)
{
    m_enableCompress = _enableCompress;
}
bool bcos::gateway::Session::enableCompress() const
{
    return m_enableCompress;
}
bcos::gateway::SessionRecvBuffer& bcos::gateway::Session::recvBuffer()
{
    return m_recvBuffer;
}
const bcos::gateway::SessionRecvBuffer& bcos::gateway::Session::recvBuffer() const
{
    return m_recvBuffer;
}
std::shared_ptr<SessionFace> bcos::gateway::SessionFactory::createSession(Host& _server,
    std::shared_ptr<SocketFace> const& _socket, MessageFactory::Ptr& _messageFactory,
    SessionCallbackManagerInterface::Ptr& _sessionCallbackManager)
{
    std::shared_ptr<Session> session =
        std::make_shared<Session>(_socket, _server, m_sessionRecvBufferSize);
    session->setHostInfo(m_hostInfo);
    session->setMessageFactory(_messageFactory);
    session->setSessionCallbackManager(_sessionCallbackManager);
    session->setAllowMaxMsgSize(m_allowMaxMsgSize);
    session->setMaxReadDataSize(m_maxReadDataSize);
    session->setMaxSendDataSize(m_maxSendDataSize);
    session->setEnableCompress(m_enableCompress);
    BCOS_LOG(INFO) << LOG_BADGE("SessionFactory") << LOG_DESC("create new session")
                   << LOG_KV("sessionRecvBufferSize", m_sessionRecvBufferSize)
                   << LOG_KV("allowMaxMsgSize", m_allowMaxMsgSize)
                   << LOG_KV("maxReadDataSize", m_maxReadDataSize)
                   << LOG_KV("maxSendDataSize", m_maxSendDataSize)
                   << LOG_KV("enableCompress", m_enableCompress);
    return session;
}
