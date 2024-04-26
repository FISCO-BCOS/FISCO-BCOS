#pragma once
#include <vector>
#include "bcos-pbft/core/ConsensusEngine.h"
#include "../interfaces/PBFTMessageFactory.h"
using namespace std;
namespace loki
{
namespace statemachine
{
class State{
    private:
        // the vector of each type of PBFT packets
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> pbft_msg_packets;
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> preprepare_msg_packets;
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> prepare_msg_packets;
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> commit_msg_packets;
        std::vector<bcos::consensus::ViewChangeMsgInterface::Ptr> view_change_msgs;
        // the current state:
        // 0 indicates PrepareReq
        // 1 indicates SignReq
        // 2 indicates CommitReq
        // 3 indicates ViewChangeReq
        int cur_state = -1;

        bool is_leader;

        // int state_id;
    public:
        State();
        ~State();
        
        // getter and setter
        // std::vector<dev::consensus::PrepareReq> get_prepare_req_packets();
        // std::vector<dev::consensus::SignReq> get_sign_req_packets();
        // std::vector<dev::consensus::CommitReq> get_commit_req_packets();
        // std::vector<dev::consensus::ViewChangeReq> get_view_change_packets();

        // void add_prepare_req_packets(dev::consensus::PrepareReq req);// not just simple add, need to change the whole state now
        // void add_sign_req_packets(dev::consensus::SignReq req);
        void add_pbft_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg);
        void add_preprepare_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg);
        void add_prepare_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg);
        void add_commit_msgs(bcos::consensus::PBFTMessageInterface::Ptr new_msg);
        void add_view_change_msgs(bcos::consensus::ViewChangeMsgInterface::Ptr new_msg);

        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> get_pbft_msgs();
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> get_preprepare_msgs();
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> get_prepare_msgs();
        std::vector<bcos::consensus::PBFTMessageInterface::Ptr> get_commit_msgs();

        std::vector<bcos::consensus::ViewChangeMsgInterface::Ptr> get_view_change_msgs();
        // void add_view_change_packets(dev::consensus::ViewChangeReq req);
        void set_cur_state(int state);
        int get_cur_state();
};

class Action{

};

class StateMachine{
    private:
        std::vector<State> states;
        
    public:
        StateMachine();
        ~StateMachine();

        // transition actions
        void transition(State cur_state, Action action);
};


}// statemachine

} // loki