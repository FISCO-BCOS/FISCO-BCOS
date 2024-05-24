#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <vector>

#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/filter/Common.h>
#include <bcos-rpc/filter/FilterRequest.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace rpc
{

class Filter
{
public:
    using Ptr = std::shared_ptr<Filter>;

    Filter(SubscriptionType type, u256 id, FilterRequest::Ptr params, bool fullTx,
        bcos::protocol::BlockNumber startBlockNumber, std::string_view group)
      : m_fullTx(fullTx),
        m_type(type),
        m_id(id),
        m_params(params),
        m_startBlockNumber(startBlockNumber),
        m_lastAccessTime(utcTime()),
        m_group(group)
    {}

    virtual ~Filter() {}

    SubscriptionType type() const { return m_type; }
    u256 id() const { return m_id; }
    int64_t startBlockNumber() const { return m_startBlockNumber.load(); }
    FilterRequest::Ptr params() const { return m_params; }
    bool fullTx() const { return m_fullTx; }
    uint64_t lastAccessTime() { return m_lastAccessTime.load(); }
    std::string_view group() const { return m_group; }

    void updateLastAccessTime() { m_lastAccessTime.store(utcTime()); }
    void setStartBlockNumber(int64_t number) { m_startBlockNumber.store(number); }
    void setId(u256 id) { m_id = id; }

private:
    bool m_fullTx;
    SubscriptionType m_type;
    u256 m_id;
    FilterRequest::Ptr m_params;
    std::atomic<int64_t> m_startBlockNumber;
    std::atomic<uint64_t> m_lastAccessTime;
    std::string m_group;
};

}  // namespace rpc
}  // namespace bcos