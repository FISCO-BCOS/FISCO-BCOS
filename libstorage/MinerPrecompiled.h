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
const char* const MINER_TYPE_MINER = "miner";
const char* const MINER_TYPE_OBSERVER = "observer";
const char* const MINER_KEY_NODEID = "node_id";
const char* const MINER_KEY_ENABLENUM = "enable_num";
const char* const MINER_PRIME_KEY = "type";
class MinerPrecompiled : public CRUDPrecompiled
{
public:
    typedef std::shared_ptr<MinerPrecompiled> Ptr;

    virtual ~MinerPrecompiled(){};

    virtual bytes call(PrecompiledContext::Ptr context, bytesConstRef param);
};

}  // namespace precompiled

}  // namespace dev
