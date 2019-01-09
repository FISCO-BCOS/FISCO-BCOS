#pragma once

namespace dev
{
namespace blockverifier
{
class TxDAG
{
public:
    virtual bool hasFinished() { return false; }

    virtual void executeUnit() {}
};
}  // namespace blockverifier
}  // namespace dev