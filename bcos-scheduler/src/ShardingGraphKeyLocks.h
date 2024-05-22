#pragma once

#include "GraphKeyLocks.h"


namespace bcos::scheduler
{
class ShardingGraphKeyLocks : public GraphKeyLocks
{
public:
    using Ptr = std::shared_ptr<ShardingGraphKeyLocks>;

    ~ShardingGraphKeyLocks() override = default;

    virtual bool acquireKeyLock(
        std::string_view contract, std::string_view key, ContextID contextID, Seq seq) override
    {
        assert(f_getAddr);
        return GraphKeyLocks::acquireKeyLock(f_getAddr(contract), key, contextID, seq);
    }

    virtual std::vector<std::string> getKeyLocksNotHoldingByContext(
        std::string_view contract, ContextID excludeContextID) const override
    {
        assert(f_getAddr);
        return GraphKeyLocks::getKeyLocksNotHoldingByContext(f_getAddr(contract), excludeContextID);
    }

    void setGetAddrHandler(std::function<std::string(const std::string_view&)> getFromFunc)
    {
        f_getAddr = std::move(getFromFunc);
    }

private:
    std::function<std::string(const std::string_view&)> f_getAddr = nullptr;
};

}  // namespace bcos::scheduler