#include "ShardingGraphKeyLocks.h"

using namespace bcos::scheduler;

ShardingGraphKeyLocks::~ShardingGraphKeyLocks() = default;

bool ShardingGraphKeyLocks::acquireKeyLock(
    std::string_view contract, std::string_view key, ContextID contextID, Seq seq)
{
    assert(f_getAddr);
    return GraphKeyLocks::acquireKeyLock(f_getAddr(contract), key, contextID, seq);
}

std::vector<std::string> ShardingGraphKeyLocks::getKeyLocksNotHoldingByContext(
    std::string_view contract, ContextID excludeContextID) const
{
    assert(f_getAddr);
    return GraphKeyLocks::getKeyLocksNotHoldingByContext(f_getAddr(contract), excludeContextID);
}

void ShardingGraphKeyLocks::setGetAddrHandler(
    std::function<std::string(const std::string_view&)> getFromFunc)
{
    f_getAddr = std::move(getFromFunc);
}