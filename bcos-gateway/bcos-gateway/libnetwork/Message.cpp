#include "Message.h"

uint32_t bcos::gateway::MessageFactory::newSeq()
{
    uint32_t seq = ++m_seq;
    return seq;
}
