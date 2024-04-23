#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <vector>

#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/filter/Common.h>
#include <bcos-rpc/filter/FilterRequest.h>

namespace bcos
{
namespace rpc
{

class Filter
{
public:
    using Ptr = std::shared_ptr<Filter>;

    Filter(SubscriptionType type, uint64_t id, FilterRequest::Ptr params, bool fullTx,
        bcos::protocol::BlockNumber startBlockNumber, boost::asio::io_service& ioService,
        int timeout, std::string_view group, std::string_view node)
      : m_type(type),
        m_id(id),
        m_params(params),
        m_fullTx(fullTx),
        m_startBlockNumber(startBlockNumber),
        m_deadline(ioService, boost::posix_time::seconds(timeout)),
        m_group(group),
        m_nodeName(node)
    {}

    virtual ~Filter() { cancelDeadLineTimer(); }

    SubscriptionType type() const { return m_type; }
    uint64_t id() const { return m_id; }
    int64_t startBlockNumber() const { return m_startBlockNumber.load(); }
    FilterRequest::Ptr params() const { return m_params; }
    bool fullTx() const { return m_fullTx; }
    boost::asio::deadline_timer& deadline() { return m_deadline; }
    std::string_view group() const { return m_group; }
    std::string_view nodeName() const { return m_nodeName; }

    void setStartBlockNumber(int64_t number) { m_startBlockNumber.store(number); }
    void cancelDeadLineTimer() { m_deadline.cancel(); }
    void setId(uint64_t id) { m_id = id; }

    bool checkGroupAndNode(std::string_view group, std::string_view node) const
    {
        return m_group == group && m_nodeName == node;
    }

private:
    SubscriptionType m_type;
    uint64_t m_id;
    FilterRequest::Ptr m_params;
    bool m_fullTx;
    std::atomic<int64_t> m_startBlockNumber;
    boost::asio::deadline_timer m_deadline;
    std::string m_group;
    std::string m_nodeName;
};

}  // namespace rpc
}  // namespace bcos