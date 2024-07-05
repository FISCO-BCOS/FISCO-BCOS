#pragma once
#include <time.h>
// #include "StateModel.h"
#include "Strategy.h"
#include "../config/PBFTConfig.h"
#include "../utilities/Common.h"
// #include <bcos-framework/interfaces/consensus/ConsensusTypeDef.h>
#include <bcos-utilities/ConcurrentQueue.h>
#include <bcos-utilities/Error.h>

// using ViewType = uint64_t;
using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::ledger;
using namespace bcos::front;
using namespace bcos::crypto;
using namespace bcos::protocol;
namespace loki
{
namespace fuzzer
{

const bool SEND_PACKAGE = true;
// send a package after some seconds;
const int SEND_INTERVAL = 5;

class Fuzzer{
    private:
        loki::statemachine::State cur_state;

        // current info
        bcos::consensus::ViewType cur_view;
        // int cur_consensusBlockHeight;
        bool leaderFailed;

        // std::shared_ptr<dev::p2p::P2PInterface> network_service;
        std::shared_ptr<bcos::consensus::PBFTConfig> consensus_config;
        int32_t protocolId;
        bcos::crypto::KeyPairInterface::Ptr keypair;
        // bcos::consensus::IDXTYPE idx;

        const bool send_package = loki::fuzzer::SEND_PACKAGE;
        const int interval = loki::fuzzer::SEND_INTERVAL;

    public:
        using Ptr = std::shared_ptr<Fuzzer>;
        // handle Prepare request 
        // use pbftMsg.prepareWithEmptyBlock to check whether the current block is empty;
        // use pbftMsg.endpoint to get the endpoint
        void handlePrePrepareMsg(bcos::consensus::PBFTMessageInterface::Ptr _preprepareMsg);

        // handle Sign request
        void handlePrepareMsg(bcos::consensus::PBFTMessageInterface::Ptr _prepareMsg);

        // handle Commit request
        void handleCommitMsg(bcos::consensus::PBFTMessageInterface::Ptr _commitMsg);

        // handle View change request
        void handleViewChangeMsg(bcos::consensus::ViewChangeMsgInterface::Ptr _viewchange);

        // handle new view msg
        void handleNewViewMsg(bcos::consensus::NewViewMsgInterface::Ptr _newviewchange);

        //handle check point msg
        void handleCheckPointMsg(bcos::consensus::PBFTMessageInterface::Ptr _checkpointmsg);

        //handle recover request
        void handleRecoverRequest(bcos::consensus::PBFTMessageInterface::Ptr _recoverRequest);

        //handle recover response
        void handleRecoverResponse(bcos::consensus::PBFTMessageInterface::Ptr _recoverResponse);

        // send pbft messages to certain nodes
        void sendToNodes(bcos::crypto::NodeIDs nodes, bcos::consensus::PBFTMessageInterface::Ptr pbftmsg);

        // send viewchange messages to certain nodes
        void sendToNodes(bcos::crypto::NodeIDs nodes, bcos::consensus::ViewChangeMsgInterface::Ptr _viewchange);

        // send new view messages to certain nodes
        void sendToNodes(bcos::crypto::NodeIDs nodes, bcos::consensus::NewViewMsgInterface::Ptr _newviewchange);

        // // send check point messages to certain nodes
        // void sendToNodes(bcos::crypt::NodeIDs, bcos::consensus::PBFTMessageInterface::Ptr _checkpointmsg);

        // // send recover requests to certain nodes
        // void sendToNodes(bcos::crypt::NodeIDs, bcos::consensus::PBFTMessageInterface::Ptr _recoverRequest);

        // // send recover responses to certain nodes
        // void sendToNodes(bcos::crypt::NodeIDs, bcos::consensus::PBFTMessageInterface::Ptr _recoverResponse);

        // protocol mutator
        void protocol_mutate();
        
        // mutate the pbft messages 
        bcos::consensus::PBFTMessageInterface::Ptr mutatePbftMsg(bcos::consensus::PBFTMessageInterface::Ptr old_msg);

        // mutate the viewchange messages
        bcos::consensus::ViewChangeMsgInterface::Ptr mutateViewChange(bcos::consensus::ViewChangeMsgInterface::Ptr old_view_change);

        // mutate the new view messages
        bcos::consensus::NewViewMsgInterface::Ptr mutateNewViewMsg(bcos::consensus::NewViewMsgInterface::Ptr old_new_view);

        // // mutate the check point messages
        // bcos::consensus::PBFTMessageInterface::Ptr mutateCheckPoint(bcos::consensus::PBFTMessageInterface::Ptr old_check_point);

        // // mutate the recover requests
        // bcos::consensus::PBFTMessageInterface::Ptr mutateRecoverRequests(bcos::consensus::PBFTMessageInterface::Ptr old_recover_req);

        // // mutate the recover responses


        void setValue(std::shared_ptr<bcos::consensus::PBFTConfig> config){
            this->consensus_config = config;
            // this->protocolId = protocol_id;
            // this->keypair = keyPair;
            // this->idx = idx;
        }

        Fuzzer(){}

};


} // namespace loki
} // namespace fuzzer