#include "Strategy.h"

namespace loki
{
namespace protocolFuzzer
{

bcos::crypto::NodeIDs random_send_nodes(loki::statemachine::State _state, bcos::crypto::NodeIDs all_consensus_nodes){
    (void)_state;
    bcos::crypto::NodeIDs res;
    int length = all_consensus_nodes.size();
    srand((unsigned)time(NULL));
    for(int i = 0 ; i < length; i++){
        if(rand() % 2 == 0){
            // random number is even, then send
            res.push_back(all_consensus_nodes[i]);
        }
    }
    return res;
}
std::vector<loki::PackageType> random_send_protocol(loki::statemachine::State _state, bcos::crypto::NodeIDs chosen_nodes){
    (void)_state;
    // use the current state to decide which type of the nodes need to be sent
    int cur_state = _state.get_cur_state();
    std::vector<loki::PackageType> res;
    srand((unsigned)time(NULL));
    for(int i = 0; i < (int)chosen_nodes.size(); i++){
        loki::PackageType packet;
        int temp = rand();
        if(cur_state == -1){
            packet = loki::PackageType(temp%4);
        }
        else{
            if(temp % 5 <= 1){
                // 40% chances to send the next packet
                packet = loki::PackageType((cur_state + 1)%4);
            }
            else if(temp % 5 == 2){
                // 20% chances to send the original packet
                packet = loki::PackageType(cur_state);
            }
            else if(temp % 5 == 3){
                // 20% chances to send the original packet
                packet = loki::PackageType((cur_state+2)%4);
            }
            else{
                // 20% chances to send the last packet
                packet = loki::PackageType((cur_state + 3)%4);
            }
        }
        // packet = crusader::PackageType(temp%4);
         
        res.push_back(packet);
    }
    return res;
}

}// protocolFuzzer
}//loki