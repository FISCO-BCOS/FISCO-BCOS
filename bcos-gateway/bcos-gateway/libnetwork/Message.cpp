#include "Message.h"

std::size_t bcos::gateway::EncodedMessage::dataSize() const
{
    return headerSize() + payloadSize();
}
std::size_t bcos::gateway::EncodedMessage::headerSize() const
{
    return header.size();
}
std::size_t bcos::gateway::EncodedMessage::payloadSize() const
{
    return payload.size();
}
uint32_t bcos::gateway::MessageFactory::newSeq()
{
    uint32_t seq = ++m_seq;
    return seq;
}
