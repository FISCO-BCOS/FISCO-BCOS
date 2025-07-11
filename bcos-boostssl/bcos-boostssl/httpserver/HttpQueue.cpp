#include "HttpQueue.h"

bcos::boostssl::http::Queue::Queue(std::size_t _limit) : m_limit(_limit) {}
void bcos::boostssl::http::Queue::setSender(std::function<void(HttpResponsePtr)> _sender)
{
    m_sender = std::move(_sender);
}
std::function<void(bcos::boostssl::http::HttpResponsePtr)> bcos::boostssl::http::Queue::sender()
    const
{
    return m_sender;
}
std::size_t bcos::boostssl::http::Queue::limit() const
{
    return m_limit;
}
void bcos::boostssl::http::Queue::setLimit(std::size_t _limit)
{
    m_limit = _limit;
}
bool bcos::boostssl::http::Queue::isFull() const
{
    return m_allResp.size() >= m_limit;
}
bool bcos::boostssl::http::Queue::onWrite()
{
    BOOST_ASSERT(!m_allResp.empty());
    auto was_full = isFull();
    m_allResp.pop_front();
    if (!m_allResp.empty())
    {
        m_sender(m_allResp.front());
    }
    return was_full;
}
void bcos::boostssl::http::Queue::enqueue(HttpResponsePtr _msg)
{
    m_allResp.push_back(std::move(_msg));
    // there was no previous work, start this one
    if (m_allResp.size() == 1)
    {
        m_sender(m_allResp.front());
    }
}
