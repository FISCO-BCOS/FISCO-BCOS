/**
 * Gateway write-path benchmark.
 *
 * Measures the cost of one business-initiated write from the service layer down to the wire, on a
 * real boost::asio TCP loopback transport (no TLS, to keep the crypto cost out of the numbers).
 *
 * Three numbers are reported per scenario:
 *   caller  - CPU burned on the thread that initiated the write, i.e. the SYNCHRONOUS part of the
 *             path (session lookup, id mapping, header encode, compression, queue push). This is
 *             the part the three optimisation branches target.
 *   process - whole-process CPU, so it adds the io thread that runs async_write and the callback
 *             that resumes the waiter.
 *   wall    - round trip as seen by the initiator (blocks until the write completed).
 *
 * Multi-threaded scenarios report aggregate throughput as well, which is where the shared_mutex
 * read-lock contention shows up.
 *
 * @file gateway_write_bench.cpp
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-gateway/gateway/PeersRouterTable.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/ServiceV2.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-gateway/protocol/GatewayNodeStatus.h"
#include "bcos-tars-protocol/protocol/GroupNodeInfoImpl.h"
#include "bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/ZstdCompress.h"

#include <boost/asio.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ba = boost::asio;
namespace bi = ba::ip;

using namespace bcos;
using namespace bcos::gateway;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------------------------
// measurement
// ---------------------------------------------------------------------------------------------

struct Stats
{
    std::string name;
    double cpuNsPerOp{0};
    double processNsPerOp{0};
    double wallNsPerOp{0};
    double opsPerSec{0};
    long long iters{0};
    int threads{1};
    double payloadBytes{0};
};

static double threadCpuNs()
{
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) * 1e9 + static_cast<double>(t.tv_nsec);
}

static double processCpuNs()
{
    timespec t{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) * 1e9 + static_cast<double>(t.tv_nsec);
}

static std::vector<Stats> g_results;
static std::mutex g_resultsMutex;

static void record(Stats s)
{
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    g_results.emplace_back(std::move(s));
}

// CPU mode: dispatch the write and return at the first real suspension (task::wait), which is
// exactly the synchronous part of the path the three optimisation branches touch. The write
// completes later on the io thread. Caller CPU is what we report; wall is meaningless here.
template <class F>
static Stats measureCpu(const std::string& name, long long iters, long long warmup, double payload,
    F&& fn, std::function<void()> drain = {})
{
    for (long long i = 0; i < warmup; ++i)
    {
        fn();
    }
    if (drain)
    {
        drain();
    }

    double c0 = threadCpuNs();
    for (long long i = 0; i < iters; ++i)
    {
        fn();
    }
    double c1 = threadCpuNs();
    if (drain)
    {
        drain();
    }

    Stats s;
    s.name = name;
    s.cpuNsPerOp = (c1 - c0) / static_cast<double>(iters);
    s.processNsPerOp = s.cpuNsPerOp;
    s.wallNsPerOp = s.cpuNsPerOp;
    s.opsPerSec = 1e9 / std::max(1.0, s.cpuNsPerOp);
    s.iters = iters;
    s.threads = 1;
    s.payloadBytes = payload;
    return s;
}

// Wall mode: block until the write has actually hit the wire, so the number is a round trip. Only
// wall is reported: task::syncWait parks the initiator with a spin/wait that would inflate caller
// CPU (a benchmark artefact - production callers co_await instead).
template <class F>
static Stats measureWall(
    const std::string& name, long long iters, long long warmup, double payload, F&& fn)
{
    for (long long i = 0; i < warmup; ++i)
    {
        fn();
    }

    auto w0 = Clock::now();
    for (long long i = 0; i < iters; ++i)
    {
        fn();
    }
    auto w1 = Clock::now();

    Stats s;
    s.name = name;
    s.wallNsPerOp = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(w1 - w0).count()) /
                    static_cast<double>(iters);
    s.cpuNsPerOp = 0;
    s.processNsPerOp = 0;
    s.opsPerSec = 1e9 / s.wallNsPerOp;
    s.iters = iters;
    s.threads = 1;
    s.payloadBytes = payload;
    return s;
}

// Single-threaded scenario: report caller CPU / process CPU / wall per op.
template <class F>
static Stats measure(const std::string& name, long long iters, long long warmup, double payload,
    F&& fn)
{
    for (long long i = 0; i < warmup; ++i)
    {
        fn();
    }

    double c0 = threadCpuNs();
    double p0 = processCpuNs();
    auto w0 = Clock::now();
    for (long long i = 0; i < iters; ++i)
    {
        fn();
    }
    auto w1 = Clock::now();
    double p1 = processCpuNs();
    double c1 = threadCpuNs();

    Stats s;
    s.name = name;
    s.cpuNsPerOp = (c1 - c0) / static_cast<double>(iters);
    s.processNsPerOp = (p1 - p0) / static_cast<double>(iters);
    s.wallNsPerOp = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(w1 - w0).count()) /
                    static_cast<double>(iters);
    s.opsPerSec = 1e9 / s.wallNsPerOp;
    s.iters = iters;
    s.threads = 1;
    s.payloadBytes = payload;
    return s;
}

// Multi-threaded scenario: `threads` initiators run `itersPerThread` ops each. Reports per-op
// wall averaged across threads (the reciprocal of per-thread throughput) and aggregate ops/s.
template <class F>
static Stats measureMt(
    const std::string& name, int threads, long long itersPerThread, long long warmup, F&& fn)
{
    for (int t = 0; t < threads; ++t)
    {
        for (long long i = 0; i < warmup; ++i)
        {
            fn();
        }
    }

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&] {
            ready.fetch_add(1);
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (long long i = 0; i < itersPerThread; ++i)
            {
                fn();
            }
        });
    }
    while (ready.load() < threads)
    {
        std::this_thread::yield();
    }

    double p0 = processCpuNs();
    auto w0 = Clock::now();
    go.store(true, std::memory_order_release);
    for (auto& w : workers)
    {
        w.join();
    }
    auto w1 = Clock::now();
    double p1 = processCpuNs();

    auto wallNs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(w1 - w0).count());
    auto totalOps = static_cast<double>(threads) * static_cast<double>(itersPerThread);

    Stats s;
    s.name = name;
    s.cpuNsPerOp = (p1 - p0) / totalOps;
    s.processNsPerOp = s.cpuNsPerOp;
    // per-thread wall per op == wall / itersPerThread (all threads run concurrently)
    s.wallNsPerOp = wallNs / static_cast<double>(itersPerThread);
    s.opsPerSec = totalOps * 1e9 / wallNs;
    s.iters = static_cast<long long>(totalOps);
    s.threads = threads;
    return s;
}

// ---------------------------------------------------------------------------------------------
// loopback transport
// ---------------------------------------------------------------------------------------------

// A SocketFace over one end of a real connected TCP loopback pair. TCP_ONLY is enough for the
// write path (awaitableWrite dispatches async_write on ref()), but a real ssl::stream is kept so
// Session::closeSocket's async_shutdown has a valid stream to touch during teardown.
class LoopbackSocket : public SocketFace
{
public:
    LoopbackSocket(std::shared_ptr<ba::io_context> _io, bi::tcp::socket _socket,
        NodeIPEndpoint _endpoint)
      : m_io(std::move(_io)),
        m_sslContext(ba::ssl::context::tlsv12),
        m_ssl(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_io, m_sslContext)),
        m_endpoint(std::move(_endpoint))
    {
        m_ssl->next_layer() = std::move(_socket);
    }

    bool isConnected() const override { return m_ssl->next_layer().is_open(); }
    void close() override
    {
        boost::system::error_code ec;
        m_ssl->next_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
        if (m_ssl->next_layer().is_open())
        {
            m_ssl->next_layer().close(ec);
        }
    }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::endpoint localEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::socket& ref() override { return m_ssl->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_ssl; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_endpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _e) override { m_endpoint = std::move(_e); }
    ba::io_context& ioService() override { return *m_io; }

private:
    std::shared_ptr<ba::io_context> m_io;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_ssl;
    NodeIPEndpoint m_endpoint;
};

// Host with the network "up" but without the accept loop, so the benchmark measures the write path
// only. m_run is the flag haveNetwork() reads.
class BenchHost : public Host
{
public:
    BenchHost(crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asio,
        std::shared_ptr<SessionFactory> _factory)
      : Host(std::move(_hash), std::move(_asio), std::move(_factory))
    {
        m_run = true;
    }
    void start() override {}
    void stop() override { m_run = false; }
};

// Exposes nothing beyond the run flag: peers are installed through the public
// Service::onConnect path, which every optimisation branch keeps, so this harness is identical
// across the branches and only the measured code differs.
class BenchService : public ServiceV2
{
public:
    BenchService(P2PInfo const& _info, RouterTableFactory::Ptr _factory, ba::io_context& _io)
      : ServiceV2(_info, std::move(_factory), _io)
    {}
    void markRunning() { m_run = true; }
};

// Exposes PeersRouterTable's protected mutators so the read-path scenario can populate it.
class BenchPeersRouterTable : public PeersRouterTable
{
public:
    BenchPeersRouterTable() : PeersRouterTable("", nullptr, nullptr) {}
    using PeersRouterTable::batchInsertNodeList;
    using PeersRouterTable::updateGatewayInfo;
};

// ---------------------------------------------------------------------------------------------
// fixture
// ---------------------------------------------------------------------------------------------

struct Peer
{
    std::shared_ptr<LoopbackSocket> socket;
    std::shared_ptr<Session> session;
    P2PSession::Ptr p2pSession;
    P2PInfo info;
    std::thread drainer;
};

struct Fixture
{
    crypto::Hash::Ptr hash = std::make_shared<crypto::Keccak256>();
    IOServicePool::Ptr pool;
    std::shared_ptr<ASIOInterface> asio;
    std::shared_ptr<BenchHost> host;
    std::shared_ptr<BenchService> service;
    P2PInfo selfInfo;
    std::vector<std::shared_ptr<Peer>> peers;

    explicit Fixture(int ioThreads)
    {
        pool = std::make_shared<IOServicePool>(static_cast<std::size_t>(ioThreads), "benchIo");
        asio = std::make_shared<ASIOInterface>(pool, "127.0.0.1", 0);
        asio->setType(ASIOInterface::TCP_ONLY);

        selfInfo.rawP2pID = makeRawId('s');
        selfInfo.p2pID = makeShortId('s');
        selfInfo.nodeIPEndpoint = NodeIPEndpoint("127.0.0.1", 30300);

        auto factory = std::make_shared<SessionFactory>(
            selfInfo, 128 * 1024, 32 * 1024 * 1024, 40 * 1024, 1024 * 1024, true);
        host = std::make_shared<BenchHost>(hash, asio, factory);

        auto routerFactory = std::make_shared<RouterTableFactoryImpl>();
        service = std::make_shared<BenchService>(selfInfo, routerFactory, *pool->getIOService());
        service->setHost(host);
        service->markRunning();
    }

    ~Fixture()
    {
        for (auto& p : peers)
        {
            if (p->socket)
            {
                p->socket->close();
            }
        }
        // let the read loops observe the close and unwind before the drainers are joined
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (auto& p : peers)
        {
            if (p->drainer.joinable())
            {
                p->drainer.join();
            }
        }
        peers.clear();
        host.reset();
        service.reset();
        asio.reset();
        pool.reset();
    }

    static std::string makeRawId(char c)
    {
        // rawP2pID is the hex text of the certificate public key: 128 chars for a 64-byte key.
        std::string s(128, '0');
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            s[i] = static_cast<char>('0' + ((static_cast<int>(c) + static_cast<int>(i)) % 16));
        }
        // make it hex
        for (auto& ch : s)
        {
            if (ch > '9')
            {
                ch = static_cast<char>('a' + (ch - '0' - 10));
            }
        }
        return s;
    }

    static std::string makeShortId(char c)
    {
        // p2pID is hash(rawP2pID): 32 raw bytes
        std::string s(32, '\0');
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            s[i] = static_cast<char>((static_cast<int>(c) * 31 + static_cast<int>(i)) & 0xFF);
        }
        return s;
    }

    // Build one real loopback peer whose reads are drained on a dedicated thread, then wire it into
    // the service exactly like Service::onConnect does.
    void addPeer(char tag, uint32_t protocolVersion)
    {
        auto io = pool->getIOService();

        bi::tcp::acceptor acceptor(*io, bi::tcp::endpoint(bi::tcp::v4(), 0));
        auto endpoint = acceptor.local_endpoint();

        bi::tcp::socket client(*io);
        client.connect(endpoint);
        bi::tcp::socket server(*io);
        acceptor.accept(server);
        acceptor.close();

        auto peer = std::make_shared<Peer>();
        peer->info.rawP2pID = makeRawId(tag);
        peer->info.p2pID = makeShortId(tag);
        peer->info.nodeIPEndpoint = NodeIPEndpoint("127.0.0.1", 20000 + (tag - 'a'));

        peer->socket = std::make_shared<LoopbackSocket>(
            io, std::move(client), peer->info.nodeIPEndpoint);
        peer->session =
            std::make_shared<Session>(peer->socket, *host, 128 * 1024, /*forceSize=*/false);
        peer->session->setHostInfo(selfInfo);
        peer->session->start();

        // Install through the production path: Service::onConnect builds the P2PSession, registers
        // it in the session table, primes the id maps and (for ServiceV2) the router table.
        service->onConnect(NetworkException(0, ""), peer->info, peer->session);

        // onConnect negotiates against the process-wide gateway protocol, whose default version
        // predates the compressed / short-id wire format. Pin V3 so the measured path matches a
        // current node (compression on, raw<->short id translation exercised).
        auto protocolInfo = std::make_shared<protocol::ProtocolInfo>(
            protocol::ProtocolModuleID::GatewayService, protocolVersion, protocolVersion);
        protocolInfo->setVersion(protocolVersion);
        if (auto installed = service->getP2PSessionByNodeId(peer->info.rawP2pID))
        {
            installed->setProtocolInfo(protocolInfo);
        }

        // drain everything the peer receives so TCP back-pressure never distorts the measurement
        peer->drainer = std::thread([srv = std::move(server)]() mutable {
            std::vector<char> buf(64 * 1024);
            boost::system::error_code ec;
            while (srv.is_open())
            {
                srv.read_some(ba::buffer(buf), ec);
                if (ec)
                {
                    break;
                }
            }
        });

        peers.emplace_back(std::move(peer));
    }

    P2pID primaryPeer() const { return peers.front()->info.rawP2pID; }
};

// ---------------------------------------------------------------------------------------------
// message construction (mirrors Gateway::sendMessageByNodeID)
// ---------------------------------------------------------------------------------------------

static Message buildUnicastMessage(
    P2pID const& srcRawId, std::string const& dstRawId, uint32_t seq, std::size_t payloadSize)
{
    Message message;
    message.setPacketType(GatewayMessageType::PeerToPeerMessage);
    message.setSeq(seq);
    message.setSrcP2PNodeID(srcRawId);
    message.setDstP2PNodeID(dstRawId);

    P2PMessageOptions options;
    options.setGroupID("group0");
    options.setModuleID(1000);
    options.setSrcNodeID(bytes(64, 0x11));
    options.mutableDstNodeIDs().push_back(bytes(64, 0x22));
    message.setOptions(std::move(options));
    return message;
}

// ---------------------------------------------------------------------------------------------
// scenarios
// ---------------------------------------------------------------------------------------------

static void runUnicastMatrix(Fixture& fx, bool quick)
{
    struct Case
    {
        std::size_t payload;
        long long cpuIters;
        long long wallIters;
    };
    // CPU mode leaves every write to complete asynchronously, so the iteration count is chosen to
    // bound the total bytes in flight (<= ~200MB) rather than the number of calls.
    std::vector<Case> cases{{512, quick ? 20000 : 200000, quick ? 500 : 3000},
        {8 * 1024, quick ? 5000 : 20000, quick ? 300 : 2000},
        {1024 * 1024, quick ? 50 : 200, quick ? 10 : 50}};

    auto dst = fx.primaryPeer();
    auto& targetSession = fx.peers.front()->session;

    for (auto const& c : cases)
    {
        bytes payload(c.payload, 0xAB);
        auto view = [&payload] { return ::ranges::views::single(bcos::ref(payload)); };

        char name[128];
        std::snprintf(name, sizeof(name), "unicast_%zuB_cpu", c.payload);
        record(measureCpu(name, c.cpuIters, std::max<long long>(20, c.cpuIters / 20),
            static_cast<double>(c.payload), [&] {
                Message msg = buildUnicastMessage(
                    fx.selfInfo.rawP2pID, dst, fx.service->newSeq(), c.payload);
                task::wait(fx.service->sendMessageByNodeID(
                    dst, msg, view(), Options{0, /*response=*/false}));
            },
            [&] {
                // let the io thread drain before the next measurement so the queue stays bounded
                while (targetSession->writeQueueSize() != 0)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }));

        char rtName[128];
        std::snprintf(rtName, sizeof(rtName), "unicast_%zuB_roundtrip", c.payload);
        record(measureWall(rtName, c.wallIters, std::max<long long>(5, c.wallIters / 10),
            static_cast<double>(c.payload), [&] {
                Message msg = buildUnicastMessage(
                    fx.selfInfo.rawP2pID, dst, fx.service->newSeq(), c.payload);
                task::syncWait(fx.service->sendMessageByNodeID(
                    dst, msg, view(), Options{0, /*response=*/false}));
            }));
    }
}

static void runBroadcastMatrix(Fixture& fx, bool quick)
{
    struct Case
    {
        std::size_t payload;
        long long iters;
    };
    std::vector<Case> cases{{512, quick ? 2000 : 20000}, {1024 * 1024, quick ? 20 : 200}};

    for (auto const& c : cases)
    {
        bytes payload(c.payload, 0xCD);
        auto view = [&payload] { return ::ranges::views::single(bcos::ref(payload)); };

        char name[128];
        std::snprintf(name, sizeof(name), "broadcast_%dpeer_%zuB_cpu", static_cast<int>(fx.peers.size()),
            c.payload);

        record(measureCpu(name, c.iters, std::max<long long>(10, c.iters / 10),
            static_cast<double>(c.payload) * static_cast<double>(fx.peers.size()), [&] {
                auto msg = std::make_shared<Message>();
                msg->setPacketType(GatewayMessageType::BroadcastMessage);
                msg->setSeq(fx.service->newSeq());
                msg->setExt(1);
                msg->setSrcP2PNodeID(fx.selfInfo.rawP2pID);
                P2PMessageOptions options;
                options.setGroupID("group0");
                options.setModuleID(1000);
                options.setSrcNodeID(bytes(64, 0x11));
                msg->setOptions(std::move(options));
                task::wait(fx.service->broadcastMessageToAll(msg, view(), Options{}));
            },
            [&] {
                bool busy = true;
                while (busy)
                {
                    busy = false;
                    for (auto& p : fx.peers)
                    {
                        busy = busy || (p->session->writeQueueSize() != 0);
                    }
                    if (busy)
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }));
    }
}

// Read-only routing hot path: the mixed lookup workload that every unicast walks.
static void runReadPathMatrix(Fixture& fx, bool quick)
{
    // ---- routing table with 64 entries ----
    auto routerFactory = std::make_shared<RouterTableFactoryImpl>();
    auto router = routerFactory->createRouterTable();
    router->setNodeID(fx.selfInfo.rawP2pID);
    router->setUnreachableDistance(10);
    std::vector<std::string> dstIds;
    for (int i = 0; i < 64; ++i)
    {
        auto id = Fixture::makeRawId(static_cast<char>('a' + (i % 26)));
        // make ids distinct
        id[0] = static_cast<char>('a' + (i / 26));
        id[1] = static_cast<char>('0' + (i % 10));
        dstIds.push_back(id);
        auto entry = routerFactory->createRouterEntry();
        entry->setDstNode(id);
        entry->setDistance(1);
        entry->setNextHop(fx.primaryPeer());
        std::set<std::string> unreachable;
        router->update(unreachable, fx.selfInfo.rawP2pID, entry);
    }

    // ---- peers router table: 2 groups x 32 nodes x 2 p2p ids ----
    BenchPeersRouterTable peersTable;
    for (char tag = 'a'; tag <= 'b'; ++tag)
    {
        auto info = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>();
        info->setGroupID("group0");
        std::vector<std::string> nodeIds;
        std::vector<protocol::ProtocolInfo::ConstPtr> protocols;
        for (int i = 0; i < 32; ++i)
        {
            nodeIds.push_back(Fixture::makeRawId(static_cast<char>('a' + (i % 26))));
            protocols.push_back(std::make_shared<protocol::ProtocolInfo>(
                protocol::ProtocolModuleID::NodeService, 3, 3));
        }
        info->setNodeIDList(std::move(nodeIds));
        info->setNodeProtocolList(std::move(protocols));
        GroupNodeInfo::Ptr base = info;
        peersTable.batchInsertNodeList(std::string(1, tag), {std::move(base)});
    }

    auto nodeIdPtr = std::make_shared<crypto::KeyImpl>(bytes(64, 0x5A));
    auto hexKey = nodeIdPtr->hex();

    long long iters = quick ? 5000 : 50000;

    record(measure("read_nodeID_hex",
        iters, 200, 0, [&] { (void)nodeIdPtr->hex(); }));

    record(measure("read_getP2PSessionByNodeId",
        iters, 200, 0, [&] { (void)fx.service->getP2PSessionByNodeId(fx.primaryPeer()); }));

    record(measure("read_getShortP2pID",
        iters, 200, 0, [&] { (void)fx.service->getShortP2pID(fx.primaryPeer()); }));

    record(measure("read_getNextHop",
        iters, 200, 0, [&] { (void)router->getNextHop(dstIds[7]); }));

    record(measure("read_getAllReachableNode_64",
        iters / 10, 50, 0, [&] { (void)router->getAllReachableNode(); }));

    // Same traversal through the visit-in-place seam a broadcast uses, so an implementation that
    // avoids the set copy shows up here while getAllReachableNode above stays a fixed reference.
    record(measure("read_forEachReachableNode_64",
        iters / 10, 50, 0, [&] {
            std::size_t visited = 0;
            router->forEachReachableNode([&](std::string const&) { ++visited; });
            asm volatile("" ::"r"(visited) : "memory");
        }));

    record(measure("read_queryP2pIDs",
        iters, 200, 0, [&] { (void)peersTable.queryP2pIDs("group0", hexKey); }));

    // The whole routing prologue of one unicast, without any payload work.
    record(measure("read_routingMix",
        iters, 200, 0, [&] {
            (void)fx.service->getP2PSessionByNodeId(fx.primaryPeer());
            (void)fx.service->getShortP2pID(fx.primaryPeer());
            (void)router->getNextHop(dstIds[7]);
            (void)peersTable.queryP2pIDs("group0", hexKey);
        }));

    // ---- broadcast fan-out bookkeeping ----
    // an empty payload set still exercises the node-list snapshot and per-peer fan-out setup
    std::vector<bytesConstRef> emptyPayloads;
    record(measure("read_broadcastCollectPeers",
        quick ? 1000 : 10000, 50, 0, [&] {
            task::syncWait(fx.service->broadcastMessageToAll(std::make_shared<Message>(),
                ::ranges::views::all(emptyPayloads), Options{}));
        }));

    // ---- copy / compress micro costs that the broadcast path pays per peer ----
    for (std::size_t size : {std::size_t(512), std::size_t(64 * 1024), std::size_t(1024 * 1024)})
    {
        Message msg = buildUnicastMessage(fx.selfInfo.rawP2pID, fx.primaryPeer(), 1, size);
        msg.setPayload(bytes(size, 0x7E));
        char name[128];
        std::snprintf(name, sizeof(name), "micro_messageCopy_%zuB", size);
        long long iters2 = size >= 1024 * 1024 ? (quick ? 200 : 2000) : (quick ? 2000 : 20000);
        record(measure(name, iters2, 50, static_cast<double>(size), [&] {
            Message copy = msg;
            asm volatile("" ::"r"(reinterpret_cast<uintptr_t>(std::addressof(copy))) : "memory");
        }));
    }

    for (std::size_t size : {std::size_t(8 * 1024), std::size_t(64 * 1024),
             std::size_t(1024 * 1024)})
    {
        bytes data(size, 0x3C);
        bytes out;
        char name[128];
        std::snprintf(name, sizeof(name), "micro_zstdCompress_%zuB", size);
        long long iters3 = size >= 1024 * 1024 ? (quick ? 50 : 500) : (quick ? 500 : 5000);
        record(measure(name, iters3, 20, static_cast<double>(size), [&] {
            out.clear();
            ZstdCompress::compress(bcos::ref(data), out, 1);
        }));
    }
}

// Multi-threaded unicast: shows how the shared read locks degrade under contention.
static void runContentionMatrix(Fixture& fx, bool quick)
{
    auto dst = fx.primaryPeer();
    bytes payload(512, 0x11);

    for (int threads : {1, 2, 4, 8})
    {
        long long perThread = quick ? 2000 : 10000;
        char name[128];
        std::snprintf(name, sizeof(name), "mt_unicast_512B_%dt_roundtrip", threads);
        auto stats = measureMt(name, threads, perThread, 20, [&] {
            Message msg = buildUnicastMessage(
                fx.selfInfo.rawP2pID, dst, fx.service->newSeq(), payload.size());
            task::syncWait(fx.service->sendMessageByNodeID(
                dst, msg, ::ranges::views::single(bcos::ref(payload)), Options{0, false}));
        });
        record(stats);
    }

    // Pure read-lock contention on x_sessions: no socket, no payload, only the lock + lookup.
    for (int threads : {1, 2, 4, 8})
    {
        long long perThread = quick ? 20000 : 100000;
        char name[128];
        std::snprintf(name, sizeof(name), "mt_getP2PSessionByNodeId_%dt", threads);
        record(measureMt(name, threads, perThread, 1000, [&] {
            (void)fx.service->getP2PSessionByNodeId(dst);
        }));
    }

    // Contrast: the same table read but returning a plain value instead of a shared_ptr. Isolates
    // lock contention from the shared_ptr control-block refcount, which is itself an atomic RMW on
    // one shared cache line and does not go away when the lock does.
    {
        auto routerFactory = std::make_shared<RouterTableFactoryImpl>();
        auto router = routerFactory->createRouterTable();
        router->setNodeID(fx.selfInfo.rawP2pID);
        std::string target;
        for (int i = 0; i < 64; ++i)
        {
            auto id = Fixture::makeRawId(static_cast<char>('a' + (i % 26)));
            id[0] = static_cast<char>('a' + (i / 26));
            id[1] = static_cast<char>('0' + (i % 10));
            auto entry = routerFactory->createRouterEntry();
            entry->setDstNode(id);
            entry->setDistance(1);
            entry->setNextHop(fx.primaryPeer());
            std::set<std::string> unreachable;
            router->update(unreachable, fx.selfInfo.rawP2pID, entry);
            if (i == 7)
            {
                target = id;
            }
        }
        for (int threads : {1, 2, 4, 8})
        {
            long long perThread = quick ? 20000 : 100000;
            char name[128];
            std::snprintf(name, sizeof(name), "mt_getNextHop_%dt", threads);
            record(measureMt(name, threads, perThread, 1000, [&] {
                (void)router->getNextHop(target);
            }));
        }
        for (int threads : {1, 2, 4, 8})
        {
            long long perThread = quick ? 20000 : 100000;
            char name[128];
            std::snprintf(name, sizeof(name), "mt_getShortP2pID_%dt", threads);
            record(measureMt(name, threads, perThread, 1000, [&] {
                (void)fx.service->getShortP2pID(fx.primaryPeer());
            }));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// reporting
// ---------------------------------------------------------------------------------------------

static void printTable()
{
    std::printf("\n");
    std::printf("%-34s %8s %12s %12s %12s %12s\n", "scenario", "threads", "caller_ns/op",
        "process_ns/op", "wall_ns/op", "Kops/s");
    std::printf("%s\n", std::string(96, '-').c_str());
    for (auto const& s : g_results)
    {
        std::printf("%-34s %8d %12.1f %12.1f %12.1f %12.1f\n", s.name.c_str(), s.threads,
            s.cpuNsPerOp, s.processNsPerOp, s.wallNsPerOp, s.opsPerSec / 1e3);
    }
    std::printf("\n");
}

static void writeJson(const std::string& path, const std::string& label)
{
    std::ofstream out(path);
    out << "{\n  \"label\": \"" << label << "\",\n  \"results\": [\n";
    for (std::size_t i = 0; i < g_results.size(); ++i)
    {
        auto const& s = g_results[i];
        out << "    {\"scenario\": \"" << s.name << "\", \"threads\": " << s.threads
            << ", \"iters\": " << s.iters << ", \"caller_cpu_ns\": " << s.cpuNsPerOp
            << ", \"process_cpu_ns\": " << s.processNsPerOp << ", \"wall_ns\": " << s.wallNsPerOp
            << ", \"ops_per_sec\": " << s.opsPerSec << "}";
        if (i + 1 != g_results.size())
        {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
}

// ---------------------------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    bool quick = false;
    bool contentionOnly = false;
    std::string jsonPath;
    std::string label = "baseline";
    int ioThreads = 4;
    int peerCount = 4;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--quick")
        {
            quick = true;
        }
        else if (arg == "--contention-only")
        {
            contentionOnly = true;
        }
        else if (arg == "--json" && i + 1 < argc)
        {
            jsonPath = argv[++i];
        }
        else if (arg == "--label" && i + 1 < argc)
        {
            label = argv[++i];
        }
        else if (arg == "--io-threads" && i + 1 < argc)
        {
            ioThreads = std::atoi(argv[++i]);
        }
        else if (arg == "--peers" && i + 1 < argc)
        {
            peerCount = std::atoi(argv[++i]);
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::printf(
                "usage: %s [--quick] [--contention-only] [--json FILE] [--label NAME]"
                " [--io-threads N] [--peers N]\n",
                argv[0]);
            return 0;
        }
    }

    // ServiceV2 negotiates a protocol and encodes it with the configured codec.
    protocol::g_BCOSConfig.setCodec(
        std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    std::printf("gateway write-path benchmark (label=%s, quick=%d, ioThreads=%d, peers=%d)\n",
        label.c_str(), static_cast<int>(quick), ioThreads, peerCount);

    {
        Fixture fx(ioThreads);
        for (int i = 0; i < peerCount; ++i)
        {
            fx.addPeer(static_cast<char>('a' + i), /*protocolVersion=*/3);
        }
        std::printf("installed %zu loopback peers\n", fx.peers.size());

        if (!contentionOnly)
        {
            runUnicastMatrix(fx, quick);
            runBroadcastMatrix(fx, quick);
            runReadPathMatrix(fx, quick);
        }
        runContentionMatrix(fx, quick);
    }

    printTable();
    if (!jsonPath.empty())
    {
        writeJson(jsonPath, label);
        std::printf("json written to %s\n", jsonPath.c_str());
    }
    return 0;
}
