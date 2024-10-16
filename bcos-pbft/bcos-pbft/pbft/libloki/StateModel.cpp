#include "StateModel.h"

namespace loki{
namespace statemachine{
    State::State(){
       
    }

    State::~State(){
       
    }

    void State::add_pbft_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg){
        if(this->pbft_msg_packets.size() > 0){
            this->pbft_msg_packets.clear();
        }
        this->pbft_msg_packets.push_back(new_msg);
    }

    void State::add_preprepare_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg){
        if(this->preprepare_msg_packets.size() > 0){
            this->preprepare_msg_packets.clear();
        }
        this->preprepare_msg_packets.push_back(new_msg);
    }

    void State::add_prepare_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg){
        if(this->prepare_msg_packets.size() > 0){
            this->prepare_msg_packets.clear();
        }
        this->prepare_msg_packets.push_back(new_msg);
    }

    void State::add_commit_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg){
        if(this->commit_msg_packets.size() > 0){
            this->commit_msg_packets.clear();
        }
        this->commit_msg_packets.push_back(new_msg);
    }

    void State::add_view_change_msgs(bcos::consensus::ViewChangeMsgInterface::Ptr new_msg){
        if(this->view_change_msgs.size() > 0){
            this->view_change_msgs.clear();
        }
        this->view_change_msgs.push_back(new_msg);
    }

    std::vector<bcos::consensus::ViewChangeMsgInterface::Ptr> State::get_view_change_msgs(){
        return this->view_change_msgs;
    }

    void State::set_cur_state(int state){
        this->cur_state = state;
    }
    int State::get_cur_state(){
        return this->cur_state;
    }
    std::vector<bcos::consensus::PBFTMessageInterface::Ptr> State::get_pbft_msgs(){
        return this->pbft_msg_packets;
    }
    std::vector<bcos::consensus::PBFTMessageInterface::Ptr> State::get_preprepare_msgs(){
        return this->preprepare_msg_packets;
    }
    std::vector<bcos::consensus::PBFTMessageInterface::Ptr> State::get_prepare_msgs(){
        return this->prepare_msg_packets;
    }
    std::vector<bcos::consensus::PBFTMessageInterface::Ptr> State::get_commit_msgs(){
        return this->commit_msg_packets;
    }

}    
}