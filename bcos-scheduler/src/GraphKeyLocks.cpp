#include "GraphKeyLocks.h"
#include "Common.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/format.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/detail/adjacency_list.hpp>
#include <boost/graph/edge_list.hpp>
#include <boost/graph/graph_selectors.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/visitors.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>

using namespace bcos::scheduler;

bool GraphKeyLocks::batchAcquireKeyLock(
    std::string_view contract, gsl::span<std::string const> keys, ContextID contextID, Seq seq)
{
    if (!keys.empty())
    {
        for (auto& it : keys)
        {
            if (!acquireKeyLock(contract, it, contextID, seq))
            {
                auto message = (boost::format("Batch acquire lock failed, contract: %s"
                                              ", key: %s, contextID: %ld, seq: %ld") %
                                contract % toHex(it) % contextID % seq)
                                   .str();
                SCHEDULER_LOG(ERROR) << message;
                BOOST_THROW_EXCEPTION(BCOS_ERROR(UnexpectedKeyLockError, message));
                return false;
            }
        }
    }

    return true;
}

bool GraphKeyLocks::acquireKeyLock(
    std::string_view contract, std::string_view key, int64_t contextID, int64_t seq)
{
    auto keyVertex = touchKeyLock(std::make_tuple(contract, key));
    auto contextVertex = touchContext(contextID);

    auto range = boost::out_edges(keyVertex, m_graph);
    for (auto it = range.first; it != range.second; ++it)
    {
        auto vertex = boost::get(VertexPropertyTag(), boost::target(*it, m_graph));
        if (std::get<0>(*vertex) != contextID)
        {
            SCHEDULER_LOG(TRACE) << boost::format(
                                        "Acquire key lock failed, request: [%s, %s, %ld, %ld] "
                                        "exists: [%ld]") %
                                        contract % key % contextID % seq % std::get<0>(*vertex);

            // Key lock holding by another context
            addEdge(contextVertex, keyVertex, seq);
            return false;
        }
    }

    // Remove all request edge
    boost::remove_edge(contextVertex, keyVertex, m_graph);

    // Add an own edge
    addEdge(keyVertex, contextVertex, seq);

    SCHEDULER_LOG(TRACE) << "Acquire key lock success, contract: " << contract << " key: " << key
                         << " contextID: " << contextID << " seq: " << seq;

    return true;
}


std::vector<std::string> GraphKeyLocks::getKeyLocksNotHoldingByContext(
    std::string_view contract, int64_t excludeContextID) const
{
    std::set<std::string> uniqueKeyLocks;

    auto range = boost::edges(m_graph);
    for (auto it = range.first; it != range.second; ++it)
    {
        auto sourceVertex = boost::get(VertexPropertyTag(), boost::source(*it, m_graph));
        auto targetVertex = boost::get(VertexPropertyTag(), boost::target(*it, m_graph));

        if (targetVertex->index() == 0 && std::get<0>(*targetVertex) != excludeContextID &&
            sourceVertex->index() == 1 && std::get<0>(std::get<1>(*sourceVertex)) == contract)
        {
            uniqueKeyLocks.emplace(std::get<1>(std::get<1>(*sourceVertex)));
        }
    }

    std::vector<std::string> keyLocks;
    keyLocks.reserve(uniqueKeyLocks.size());
    for (auto& it : uniqueKeyLocks)
    {
        keyLocks.emplace_back(std::move(it));
    }

    return keyLocks;
}

void GraphKeyLocks::releaseKeyLocks(int64_t contextID, int64_t seq)
{
    SCHEDULER_LOG(TRACE) << "Release key lock, contextID: " << contextID << " seq: " << seq;
    auto vertex = touchContext(contextID);

    auto edgeRemoveFunc = [seq, graph = &m_graph](auto range) mutable -> bool {
        size_t total = 0;
        size_t removed = 0;

        for (auto next = range.first; range.first != range.second; range.first = next)
        {
            ++total;
            ++next;
            auto edgeSeq = boost::get(EdgePropertyTag(), *range.first);
            if (edgeSeq == seq)
            {
                ++removed;
                if (bcos::LogLevel::TRACE >= bcos::c_fileLogLevel)
                {
                    auto source =
                        boost::get(VertexPropertyTag(), boost::source(*range.first, *graph));
                    auto target =
                        boost::get(VertexPropertyTag(), boost::target(*range.first, *graph));
                    if (target->index() == 1)
                    {
                        source = target;
                    }

                    const auto& [contract, key] = std::get<1>(*source);
                    SCHEDULER_LOG(TRACE)
                        << "Releasing key lock, contract: " << contract << " key: " << key;
                }
                boost::remove_edge(*range.first, *graph);
            }
        }

        return total == removed;
    };

    auto clearedIn = edgeRemoveFunc(boost::in_edges(vertex, m_graph));
    auto clearedOut = edgeRemoveFunc(boost::out_edges(vertex, m_graph));

    if (clearedIn && clearedOut)
    {
        // All edge had removed, delete the vertex
        boost::remove_vertex(vertex, m_graph);
        m_vertexes.erase(contextID);
    }
}

bool GraphKeyLocks::detectDeadLock(ContextID contextID)
{
    struct GraphVisitor
    {
        GraphVisitor(bool& backEdge) : m_backEdge(backEdge) {}

        void initialize_vertex(VertexID, const Graph&) {}
        void start_vertex(VertexID, const Graph&) {}
        void discover_vertex(VertexID, const Graph&) {}
        void examine_edge(EdgeID, const Graph&) {}
        void tree_edge(EdgeID, const Graph&) {}
        void forward_or_cross_edge(EdgeID, const Graph&) {}
        void finish_edge(EdgeID, const Graph&) {}
        void finish_vertex(VertexID, const Graph&) {}

        void back_edge(EdgeID edgeID, const Graph&) const
        {
            auto edgeSeq = boost::get(EdgePropertyTag(), edgeID);
            SCHEDULER_LOG(TRACE) << "Detected back edge seq: " << edgeSeq;

            m_backEdge = true;
        }

        bool& m_backEdge;
    };

    auto it = m_vertexes.find(bcos::scheduler::GraphKeyLocks::Vertex(contextID));
    if (it == m_vertexes.end())
    {
        // No vertex, may be removed
        return false;
    }

    if (boost::in_degree(it->second, m_graph) == 0)
    {
        // No in degree, not holding key lock
        return false;
    }

    std::map<VertexID, boost::default_color_type> vertexColors;
    bool hasDeadLock = false;
    boost::depth_first_visit(m_graph, it->second, GraphVisitor(hasDeadLock),
        boost::make_assoc_property_map(vertexColors),
        [&hasDeadLock](VertexID, const Graph&) { return hasDeadLock; });


    return hasDeadLock;
}

GraphKeyLocks::VertexID GraphKeyLocks::touchContext(int64_t contextID)
{
    auto [it, inserted] = m_vertexes.emplace(Vertex(contextID), VertexID());
    if (inserted)
    {
        it->second = boost::add_vertex(&(it->first), m_graph);
    }
    auto contextVertexID = it->second;

    return contextVertexID;
}

GraphKeyLocks::VertexID GraphKeyLocks::touchKeyLock(KeyLockView keyLockView)
{
    auto contractKeyView = keyLockView;
    auto it = m_vertexes.lower_bound(contractKeyView);
    if (it != m_vertexes.end() && it->first == contractKeyView)
    {
        return it->second;
    }

    auto inserted = m_vertexes.emplace_hint(it,
        Vertex(std::make_tuple(
            std::string(std::get<0>(contractKeyView)), std::string(std::get<1>(contractKeyView)))),
        VertexID());
    inserted->second = boost::add_vertex(&(inserted->first), m_graph);
    return inserted->second;
}

void GraphKeyLocks::addEdge(VertexID source, VertexID target, int64_t seq)
{
    bool exists = false;

    auto range = boost::edge_range(source, target, m_graph);
    for (auto it = range.first; it != range.second; ++it)
    {
        auto edgeSeq = boost::get(EdgePropertyTag(), *it);
        if (edgeSeq == seq)
        {
            exists = true;
        }
    }

    if (!exists)
    {
        boost::add_edge(source, target, seq, m_graph);
    }
}

void GraphKeyLocks::removeEdge(VertexID source, VertexID target, int64_t seq)
{
    auto range = boost::edge_range(source, target, m_graph);
    for (auto it = range.first; it != range.second; ++it)
    {
        auto edgeSeq = boost::get(EdgePropertyTag(), *it);
        if (edgeSeq == seq)
        {
            boost::remove_edge(*it, m_graph);
            break;
        }
    }
}