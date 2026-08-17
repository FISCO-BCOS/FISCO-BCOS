#pragma once

#include "GraphKeyLocks.h"


namespace bcos::scheduler
{
class ShardingGraphKeyLocks : public GraphKeyLocks
{
public:
    using Ptr = std::shared_ptr<ShardingGraphKeyLocks>;

    ~ShardingGraphKeyLocks() override;

    virtual bool acquireKeyLock(
        std::string_view contract, std::string_view key, ContextID contextID, Seq seq) override;

    virtual std::vector<std::string> getKeyLocksNotHoldingByContext(
        std::string_view contract, ContextID excludeContextID) const override;

    void setGetAddrHandler(std::function<std::string(const std::string_view&)> getFromFunc);

private:
    std::function<std::string(const std::string_view&)> f_getAddr = nullptr;
};

}  // namespace bcos::scheduler