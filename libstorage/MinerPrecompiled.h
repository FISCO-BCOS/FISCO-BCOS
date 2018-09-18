#pragma once
#include "libstorage/CRUDPrecompiled.h"

namespace dev
{

namespace precompiled
{
#if 0
contract Miner {
    function add(string) public constant returns(bool);
    function cancel(string) public returns(bool);
}
{
    "b0c8f9dc": "add(string)",
    "80599e4b": "remove(string)"
}
#endif

class MinerPrecompiled : public CRUDPrecompiled
{
public:
    typedef std::shared_ptr<MinerPrecompiled> Ptr;
    // virtual void beforeBlock(PrecompiledContext::Ptr) {};
    // virtual void afterBlock(PrecompiledContext::Ptr, bool commit) {};

    virtual ~MinerPrecompiled(){};

    virtual bytes call(PrecompiledContext::Ptr context, bytesConstRef param);
};

}  // namespace precompiled

}  // namespace dev
