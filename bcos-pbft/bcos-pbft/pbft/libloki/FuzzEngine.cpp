#include "FuzzEngine.h"

#include <thread>

namespace loki
{
namespace fuzzer
{
void Fuzzer::handlePrePrepareMsg(bcos::consensus::PBFTMessageInterface::Ptr _preprepareMsg){
    // change the current state
    PBFT_LOG(INFO) <<"Receive pre-prepare message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_pbft_msgs(_preprepareMsg);
    this->cur_state.add_preprepare_msgs(_preprepareMsg);
    this->cur_view = _preprepareMsg->view();
    this->cur_state.set_cur_state(0);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handlePrepareMsg(bcos::consensus::PBFTMessageInterface::Ptr _prepareMsg){
    // change the current state
    PBFT_LOG(INFO) <<"Receive prepare message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_pbft_msgs(_prepareMsg);
    this->cur_state.add_prepare_msgs(_prepareMsg);
    this->cur_view = _prepareMsg->view();
    this->cur_state.set_cur_state(1);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handleCommitMsg(bcos::consensus::PBFTMessageInterface::Ptr _commitMsg){
    // change the current state
    PBFT_LOG(INFO) <<"Receive commit message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_pbft_msgs(_commitMsg);
    this->cur_state.add_commit_msgs(_commitMsg);
    this->cur_view = _commitMsg->view();
    this->cur_state.set_cur_state(2);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handleViewChangeMsg(bcos::consensus::ViewChangeMsgInterface::Ptr _viewchange){
    // change the current state
    PBFT_LOG(INFO) <<"Receive view change message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_view_change_msgs(_viewchange);
    this->cur_state.set_cur_state(3);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handleNewViewMsg(bcos::consensus::NewViewMsgInterface::Ptr _newviewchange){
    (void)_newviewchange;
    PBFT_LOG(INFO) <<"Receive new change message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.set_cur_state(4);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handleCheckPointMsg(bcos::consensus::PBFTMessageInterface::Ptr _checkpointmsg){
    PBFT_LOG(INFO) <<"Receive check point message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_pbft_msgs(_checkpointmsg);
    this->cur_state.set_cur_state(5);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handleRecoverRequest(bcos::consensus::PBFTMessageInterface::Ptr _recoverRequest){
    PBFT_LOG(INFO) <<"Receive recover request message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_pbft_msgs(_recoverRequest);
    this->cur_state.set_cur_state(6);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::handleRecoverResponse(bcos::consensus::PBFTMessageInterface::Ptr _recoverResponse){
    PBFT_LOG(INFO) <<"Receive recover response message; "<< "we are using the LOKI node!!!!#####" << endl;
    this->cur_state.add_pbft_msgs(_recoverResponse);
    this->cur_state.set_cur_state(7);
    // send the random packages
    this->protocol_mutate();
}

void Fuzzer::sendToNodes(bcos::crypto::NodeIDs nodes, bcos::consensus::PBFTMessageInterface::Ptr pbftmsg){
    auto encodedData = this->consensus_config->codec()->encode(pbftmsg);
    this->consensus_config->frontService()->asyncSendMessageByNodeIDs(
        bcos::protocol::ModuleID::PBFT, nodes, ref(*encodedData));
}

void Fuzzer::sendToNodes(bcos::crypto::NodeIDs nodes, bcos::consensus::ViewChangeMsgInterface::Ptr _viewchange){
    auto encodedData = this->consensus_config->codec()->encode(_viewchange);
    this->consensus_config->frontService()->asyncSendMessageByNodeIDs(
        bcos::protocol::ModuleID::PBFT, nodes, ref(*encodedData));
}

void Fuzzer::sendToNodes(bcos::crypto::NodeIDs nodes, bcos::consensus::NewViewMsgInterface::Ptr _newviewchange){
    auto encodedData = this->consensus_config->codec()->encode(_newviewchange);
    this->consensus_config->frontService()->asyncSendMessageByNodeIDs(
        bcos::protocol::ModuleID::PBFT, nodes, ref(*encodedData));
}

bcos::consensus::PBFTMessageInterface::Ptr Fuzzer::mutatePbftMsg(bcos::consensus::PBFTMessageInterface::Ptr old_msg){
    bcos::consensus::PBFTMessageInterface::Ptr new_msg = old_msg;
    srand(time(NULL));
    int view_temp = rand() % 5;
    int timestamp_temp = rand() % 3;
    // int type_temp = rand() % 3;
    int version_temp = rand() % 3;
    int from_temp = rand() % 3;
    bool choose_change_view = (rand() % 2 == 0);
    bool choose_change_hash = (rand() % 2 == 0);
    bool choose_change_type = (rand() % 2 == 0);
    bool choose_change_from = (rand() % 2 == 0);
    bool choose_change_version = (rand() % 2 == 0);
    bool choose_change_timestamp = (rand() % 2 == 0);
    if (choose_change_view){
        if(view_temp == 0){
            new_msg->setView(old_msg->view());
        }
        else if (view_temp == 1){
            new_msg->setView(old_msg->view() - 100);
        }
        else if (view_temp == 2){
            new_msg->setView(old_msg->view() + 100);
        }
        else if (view_temp == 3){
            new_msg->setView(old_msg->view() - 1);
        }
        else {
            new_msg->setView(old_msg->view() + 1);
        }
    }
    if (choose_change_timestamp){
        if(timestamp_temp == 0){
            new_msg->setTimestamp(old_msg->timestamp());
        }
        else if (timestamp_temp == 1){
            new_msg->setTimestamp(old_msg->timestamp() + 1000);
        }
        else{
            new_msg->setTimestamp(old_msg->timestamp() - 1000);
        }
    }
    if (choose_change_from){
        if(from_temp == 0){
            new_msg->setGeneratedFrom(old_msg->generatedFrom() + 1);
        }
        else if (from_temp == 1){
            new_msg->setGeneratedFrom(old_msg->generatedFrom() - 1);
        }
        else{
            new_msg->setGeneratedFrom(rand());
        }
    }
    if (choose_change_hash){
        // only set the hash to empty, should add a random hash
        const bcos::crypto::HashType newhash;
        new_msg->setHash(newhash);
    }
    if (choose_change_version){
        if(version_temp == 0){
            new_msg->setVersion(old_msg->version() + 1);
        }
        else if (version_temp == 1){
            new_msg->setVersion(old_msg->version() - 1);
        }
        else{
            new_msg->setVersion(rand());
        }
    }
    if (choose_change_type){
        // if(type_temp == 0){
        //     new_msg->setPacketType(old_msg->packetType() + 1);
        // }
        // else if (type_temp == 1){
        //     new_msg->setPacketType(old_msg->packetType() - 1);
        // }
        // else{
        new_msg->setPacketType(bcos::consensus::PacketType(rand() % 12));
        // }

    }
    return new_msg;
}

bcos::consensus::ViewChangeMsgInterface::Ptr Fuzzer::mutateViewChange(bcos::consensus::ViewChangeMsgInterface::Ptr old_msg){
    bcos::consensus::ViewChangeMsgInterface::Ptr new_msg = old_msg;
    srand(time(NULL));
    int view_temp = rand() % 5;
    int timestamp_temp = rand() % 3;
    bool choose_change_view = (rand() % 2 == 0);
    bool choose_change_timestamp = (rand() % 2 == 0);
    if (choose_change_view){
        if(view_temp == 0){
            new_msg->setView(old_msg->view());
        }
        else if (view_temp == 1){
            new_msg->setView(old_msg->view() - 100);
        }
        else if (view_temp == 2){
            new_msg->setView(old_msg->view() + 100);
        }
        else if (view_temp == 3){
            new_msg->setView(old_msg->view() - 1);
        }
        else {
            new_msg->setView(old_msg->view() + 1);
        }
    }
    if (choose_change_timestamp){
        if(timestamp_temp == 0){
            new_msg->setTimestamp(old_msg->timestamp());
        }
        else if (timestamp_temp == 1){
            new_msg->setTimestamp(old_msg->timestamp() + 1000);
        }
        else{
            new_msg->setTimestamp(old_msg->timestamp() - 1000);
        }
    }
    return new_msg;
}

bcos::consensus::NewViewMsgInterface::Ptr Fuzzer::mutateNewViewMsg(bcos::consensus::NewViewMsgInterface::Ptr old_msg){
    bcos::consensus::NewViewMsgInterface::Ptr new_msg = old_msg;
    srand(time(NULL));
    int view_temp = rand() % 5;
    int timestamp_temp = rand() % 3;
    bool choose_change_view = (rand() % 2 == 0);
    bool choose_change_timestamp = (rand() % 2 == 0);
    if (choose_change_view){
        if(view_temp == 0){
            new_msg->setView(old_msg->view());
        }
        else if (view_temp == 1){
            new_msg->setView(old_msg->view() - 100);
        }
        else if (view_temp == 2){
            new_msg->setView(old_msg->view() + 100);
        }
        else if (view_temp == 3){
            new_msg->setView(old_msg->view() - 1);
        }
        else {
            new_msg->setView(old_msg->view() + 1);
        }
    }
    if (choose_change_timestamp){
        if(timestamp_temp == 0){
            new_msg->setTimestamp(old_msg->timestamp());
        }
        else if (timestamp_temp == 1){
            new_msg->setTimestamp(old_msg->timestamp() + 1000);
        }
        else{
            new_msg->setTimestamp(old_msg->timestamp() - 1000);
        }
    }
    return new_msg;
}

void Fuzzer::protocol_mutate(){
    // TODO: write this function
    // TODO: implement intitive sending packetes in pbftengine
    // TODO: implement more strategies in strategy.cpp
    // TODO: add handler before the original ones
    PBFT_LOG(INFO) <<"LOKI executes protocol mutation";
    // currently the strategy of nodes is just randomly choosing
    bcos::crypto::NodeIDs sending_nodes = loki::protocolFuzzer::random_send_nodes(this->cur_state, this->consensus_config->consensusNodeIDList());
    // currently the strategy of protocol types is just randomly choosing 
    std::vector<loki::PackageType> chosen_packetes = loki::protocolFuzzer::random_send_protocol(this->cur_state, sending_nodes);
    PBFT_LOG(DEBUG)<< "send_nodes' size is "<<sending_nodes.size()<<" and send_packets' size is "<<chosen_packetes.size();
    if(sending_nodes.size() != chosen_packetes.size()){
        throw std::runtime_error("FuzzEngine Error: the number of sending nodes and sending packetes are not equal!!");
    }
    for(int i = 0; i < (int)sending_nodes.size(); i++){
        PBFT_LOG(DEBUG)<< "send_packets is  "<<chosen_packetes[i];
        // PBFT_LOG(DEBUG)<< "we have received prepare req?  "<<cur_state.get_prepare_req_packets().size();
        switch (chosen_packetes[i])
        {
        case loki::PREPARE_REQ:
            if (this->cur_state.get_preprepare_msgs().size() > 0){
                // current we have some preprepare messages
                auto old_req = this->cur_state.get_preprepare_msgs()[0];
                bcos::consensus::PBFTMessageInterface::Ptr new_req = this->mutatePbftMsg(old_req);
                this->sendToNodes(sending_nodes,new_req);
            }
            else{
                // current we have not received any message
                auto new_req = this->consensus_config->pbftMessageFactory()->createPBFTMsg();
                this->sendToNodes(sending_nodes,new_req);
            }
            break;
        case loki::SIGN_REQ:
            if (this->cur_state.get_prepare_msgs().size() > 0){
                // current we have some prepare messasges
                auto old_req = this->cur_state.get_prepare_msgs()[0];
                auto new_req = this->mutatePbftMsg(old_req);
                this->sendToNodes(sending_nodes,new_req);
            }
            else{
                // current we have not received any message
                auto new_req = this->consensus_config->pbftMessageFactory()->createPBFTMsg();
                this->sendToNodes(sending_nodes,new_req);
            }
            break;
        case loki::COMMIT_REQ:
            if(this->cur_state.get_commit_msgs().size() > 0){
                // current we have some commit messages
                auto old_msg = this->cur_state.get_commit_msgs()[0];
                auto new_msg = this->mutatePbftMsg(old_msg);
                this->sendToNodes(sending_nodes,new_msg);
            }
            else{
                // current we have not received any message
                auto new_req = this->consensus_config->pbftMessageFactory()->createPBFTMsg();
                this->sendToNodes(sending_nodes,new_req);
            }
            break;
        case loki::VIEWCHANGE_REQ:
            if(this->cur_state.get_view_change_msgs().size() > 0){
                // current we have some view change messages
                auto old_msg = this->cur_state.get_view_change_msgs()[0];
                auto new_msg = this->mutateViewChange(old_msg);
                this->sendToNodes(sending_nodes,new_msg);
            }
            else{
                // current we have received no view change messages
                auto new_req = this->consensus_config->pbftMessageFactory()->createViewChangeMsg();
                this->sendToNodes(sending_nodes,new_req);
            }
            break;
        default:
            break;
        }
        
    }

}

} // fuzzer
} // loki