#include "RouterTableInterface.h"
#include "../Common.h"

std::string bcos::gateway::RouterTableEntryInterface::printDstNode() const
{
    return printShortP2pID(dstNode());
}

std::string bcos::gateway::RouterTableEntryInterface::printNextHop() const
{
    return printShortP2pID(nextHop());
}