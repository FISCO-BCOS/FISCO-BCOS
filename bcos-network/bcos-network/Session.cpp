
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 *
 * Only the non-template SessionRecvBuffer / Payload definitions live here. BasicSession /
 * BasicSessionFactory are templates over the frame decoder (see FrameMeta.h); their members are
 * defined inside the classes in Session.h and are instantiated by the concrete decoder's TU
 * (the gateway: libp2p's P2PDecoder).
 */

#include "bcos-network/Session.h"
#include <range/v3/numeric/accumulate.hpp>

using namespace bcos;
using namespace bcos::network;

size_t bcos::network::Payload::size() const
{
    return ::ranges::accumulate(
        m_data, size_t(0), [](size_t sum, const bytesConstRef& ref) { return sum + ref.size(); });
}
std::size_t bcos::network::SessionRecvBuffer::readPos() const
{
    return m_readPos;
}
std::size_t bcos::network::SessionRecvBuffer::writePos() const
{
    return m_writePos;
}
std::size_t bcos::network::SessionRecvBuffer::dataSize() const
{
    return m_writePos - m_readPos;
}
size_t bcos::network::SessionRecvBuffer::recvBufferSize() const
{
    return m_recvBufferSize;
}
bool bcos::network::SessionRecvBuffer::onRead(std::size_t _dataSize)
{
    if (m_readPos + _dataSize <= m_writePos)
    {
        m_readPos += _dataSize;
        return true;
    }
    return false;
}
bool bcos::network::SessionRecvBuffer::onWrite(std::size_t _dataSize)
{
    if (m_writePos + _dataSize <= m_recvBufferSize)
    {
        m_writePos += _dataSize;
        return true;
    }
    return false;
}
bool bcos::network::SessionRecvBuffer::resizeBuffer(size_t _bufferSize)
{
    if (_bufferSize > m_recvBufferSize)
    {
        m_recvBuffer.resize(_bufferSize);
        m_recvBufferSize = _bufferSize;

        return true;
    }

    return false;
}
void bcos::network::SessionRecvBuffer::moveToHeader()
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
std::size_t bcos::network::SessionRecvBuffer::takeStorage(bytes& out, std::size_t wireLen)
{
    const auto frameStart = m_readPos;          // also the frame's plaintext offset in `out`
    const auto wireEnd = frameStart + wireLen;  // end of the WIRE frame (what leaves with `out`)
    const std::size_t tail = m_writePos - wireEnd;

    out = std::move(m_recvBuffer);
    // Reseed at the SAME (grown) size, not at a small floor: a take happens only once the buffer
    // has grown to hold a large frame, and a small reseed would make the NEXT large frame
    // re-enter the NeedMoreData grow path — a fresh allocate plus a zero-fill of 2*length per
    // frame, the cost this path exists to avoid. Keeping the size keeps the allocation
    // one-per-take instead of grow-from-scratch per frame.
    m_recvBuffer.resize(m_recvBufferSize);
    if (tail > 0)
    {
        std::memcpy(m_recvBuffer.data(), out.data() + wireEnd, tail);
    }
    // Drop the stolen tail bytes (size-only, no relocation). Truncating at the WIRE end also
    // keeps the whole plaintext: for every current decoder the plaintext begins at the wire
    // frame's first byte and is no longer than it (RlpxFrameDecoder's plaintext is shorter).
    out.resize(wireEnd);
    // The consumer retains this storage (Message::decodeOwned / the RLPx facade) while the recv
    // channel charges only meta.frameSize against its byte budget: release the surplus capacity
    // (the buffer had grown to ~2x the frame) so the heap a full queue pins matches what the
    // channel accounts for.
    out.shrink_to_fit();
    m_readPos = 0;
    m_writePos = tail;
    return frameStart;
}
bcos::bytesConstRef bcos::network::SessionRecvBuffer::asReadBuffer() const
{
    return {m_recvBuffer.data() + m_readPos, m_writePos - m_readPos};
}
bcos::bytesRef bcos::network::SessionRecvBuffer::asWriteBuffer()
{
    return {m_recvBuffer.data() + m_writePos, m_recvBufferSize - m_writePos};
}
