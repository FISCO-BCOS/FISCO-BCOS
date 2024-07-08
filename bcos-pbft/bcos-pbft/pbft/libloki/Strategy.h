#pragma once
#include <vector>
#include "StateModel.h"
#include "Common.h"

namespace loki
{
namespace protocolFuzzer
{
bcos::crypto::NodeIDs random_send_nodes(loki::statemachine::State _state, bcos::crypto::NodeIDs all_consensus_nodes);
std::vector<loki::PackageType> random_send_protocol(loki::statemachine::State _state, bcos::crypto::NodeIDs chosen_nodes);
}// protocolFuzzer
}//loki