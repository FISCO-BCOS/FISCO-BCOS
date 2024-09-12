#pragma once
#include "bcos-utilities/Common.h"
#include <string>
#include <vector>

namespace bcos::ledger
{
struct ConsensusNode
{
    std::string nodeID;
    u256 voteWeight;
    std::string type;
    std::string enableNumber;

    template <typename Archive>
    void serialize(Archive& archive, [[maybe_unused]] unsigned int version)
    {
        archive & nodeID;
        archive & voteWeight;
        archive & type;
        archive & enableNumber;
    }
};

using ConsensusNodeList = std::vector<ledger::ConsensusNode>;
ConsensusNodeList decodeConsensusList(const std::string_view& value);
std::string encodeConsensusList(const bcos::ledger::ConsensusNodeList& consensusList);

}  // namespace bcos::ledger