
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 *
 * Only the non-template SessionRecvBuffer / Payload definitions live here. BasicSession /
 * BasicSessionFactory are templates over the frame decoder (see FrameMeta.h); their member
 * definitions live at the bottom of Session.h and are instantiated by the concrete decoder's TU
 * (the gateway: libp2p's P2PDecoder).
 */

#include "bcos-gateway/libnetwork/Session.h"
#include <range/v3/numeric/accumulate.hpp>

using namespace bcos;
using namespace bcos::gateway;

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
