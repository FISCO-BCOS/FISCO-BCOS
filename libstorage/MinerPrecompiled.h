#pragma once
#include "libstorage/CRUDPrecompiled.h"

namespace dev
{

namespace precompiled
{
#if 0
contract Miner {
    function add(string) public constant returns();
    function cancel(string) public returns();
}
{
    "b0c8f9dc": "add(string)",
    "80599e4b": "remove(string)"
}
#endif
/// \brief Sign of the miner is valid or not
const char* const NODE_TYPE = "type";
const char* const NODE_TYPE_MINER = "miner";
const char* const NODE_TYPE_OBSERVER = "observer";
const char* const NODE_KEY_NODEID = "node_id";
const char* const NODE_KEY_ENABLENUM = "enable_num";
const char* const PRIME_KEY = "node";
class MinerPrecompiled : public CRUDPrecompiled
{
public:
    typedef std::shared_ptr<MinerPrecompiled> Ptr;

    virtual ~MinerPrecompiled(){};

    virtual bytes call(PrecompiledContext::Ptr context, bytesConstRef param);
};

}  // namespace precompiled

}  // namespace dev
