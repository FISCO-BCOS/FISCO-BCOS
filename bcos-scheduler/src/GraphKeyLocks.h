#pragma once

#include "Common.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/edge_list.hpp>
#include <boost/graph/graph_selectors.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/visitors.hpp>
#include <any>
#include <forward_list>
#include <functional>
#include <gsl/span>
#include <set>
#include <string_view>
#include <variant>

#define KEY_LOCK_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SCHEDULER") << LOG_BADGE("KEY_LOCK")
// #define KEY_LOCK_LOG(LEVEL) std::cout << LOG_BADGE("KEY_LOCK")

namespace bcos::scheduler
{
class GraphKeyLocks
{
public:
    using Ptr = std::shared_ptr<GraphKeyLocks>;
    using KeyLock = std::tuple<std::string, std::string>;
    using KeyLockView = std::tuple<std::string_view, std::string_view>;

    using ContractView = std::string_view;
    using KeyView = std::string_view;

    GraphKeyLocks() = default;
    GraphKeyLocks(const GraphKeyLocks&) = delete;
    GraphKeyLocks(GraphKeyLocks&&) = delete;
    GraphKeyLocks& operator=(const GraphKeyLocks&) = delete;
    GraphKeyLocks& operator=(GraphKeyLocks&&) = delete;

    bool batchAcquireKeyLock(std::string_view contract, gsl::span<std::string const> keyLocks,
        ContextID contextID, Seq seq);

    bool acquireKeyLock(
        std::string_view contract, std::string_view key, ContextID contextID, Seq seq);

    std::vector<std::string> getKeyLocksNotHoldingByContext(
        std::string_view contract, ContextID excludeContextID) const;

    void releaseKeyLocks(ContextID contextID, Seq seq);

    bool detectDeadLock(ContextID contextID);

    struct Vertex : public std::variant<ContextID, KeyLock>
    {
        using std::variant<ContextID, KeyLock>::variant;

        bool operator==(const KeyLockView& rhs) const
        {
            if (index() != 1)
            {
                return false;
            }

            auto view = std::make_tuple(std::string_view(std::get<0>(std::get<1>(*this))),
                std::string_view(std::get<1>(std::get<1>(*this))));
            return view == rhs;
        }
    };

private:
    struct VertexIterator
    {
        using kind = boost::vertex_property_tag;
    };
    using VertexProperty = boost::property<VertexIterator, const Vertex*>;
    struct EdgeSeq
    {
        using kind = boost::edge_property_tag;
    };
    using EdgeProperty = boost::property<EdgeSeq, int64_t>;

    using Graph = boost::adjacency_list<boost::multisetS, boost::multisetS, boost::bidirectionalS,
        VertexProperty, EdgeProperty>;
    using VertexPropertyTag = boost::property_map<Graph, VertexIterator>::const_type;
    using EdgePropertyTag = boost::property_map<Graph, EdgeSeq>::const_type;
    using VertexID = Graph::vertex_descriptor;
    using EdgeID = Graph::edge_descriptor;

    Graph m_graph;
    std::map<Vertex, VertexID, std::less<>> m_vertexes;

    VertexID touchContext(ContextID contextID);
    VertexID touchKeyLock(KeyLockView keylockView);

    void addEdge(VertexID source, VertexID target, Seq seq);
    void removeEdge(VertexID source, VertexID target, Seq seq);
};

}  // namespace bcos::scheduler

namespace std
{
inline bool operator<(const bcos::scheduler::GraphKeyLocks::Vertex& lhs,
    const bcos::scheduler::GraphKeyLocks::KeyLockView& rhs)
{
    if (lhs.index() != 1)
    {
        return true;
    }

    auto view = std::make_tuple(std::string_view(std::get<0>(std::get<1>(lhs))),
        std::string_view(std::get<1>(std::get<1>(lhs))));
    return view < rhs;
}

inline bool operator<(const bcos::scheduler::GraphKeyLocks::KeyLockView& lhs,
    const bcos::scheduler::GraphKeyLocks::Vertex& rhs)
{
    if (rhs.index() != 1)
    {
        return false;
    }

    auto view = std::make_tuple(std::string_view(std::get<0>(std::get<1>(rhs))),
        std::string_view(std::get<1>(std::get<1>(rhs))));
    return lhs < view;
}
}  // namespace std