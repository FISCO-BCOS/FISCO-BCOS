pragma solidity ^0.4.4;

import "ConsensusControlAction.sol";
import "LibEthLog.sol";

/**
 * 要求所有机构都有签名 
 */
contract ConsensusControl is ConsensusControlAction {
    using LibEthLog for *;

    function ConsensusControl (address systemAddr) ConsensusControlAction (systemAddr) {
        LibEthLog.DEBUG().append("[ConsensusControl] ######### current agency list ######## length:").append(agencyList.length).commit();
        // for (uint i=0; i < agencyList.length; i++) {
        //     LibEthLog.DEBUG().append("[ConsensusControl]").append(i)
        //     .append(":")
        //     .append(bytes32ToString(agencyList[i]))
        //     .append(":")
        //     .append(agencyCountMap[agencyList[i]])
        //     .commit();
        // }
        // LibEthLog.DEBUG().append("[ConsensusControl] ######### end ########:").commit();
    }
    
    function control(bytes32[] info, uint[] num) external constant returns (bool) {
        LibEthLog.DEBUG().append("[ConsensusControl] control start info.length:").append(info.length).commit();
        uint count = 0;
        for(uint i=0; i < info.length; i++) {
            //agencyCountMap[info[i]] != 0 存在这个机构
            // num[i] != 0 这个机构传进来个数大于0
            if (agencyCountMap[info[i]] != 0 && num[i] != 0) { 
                count += 1;
            }
        }
        
        LibEthLog.DEBUG().append("[ConsensusControl] control count:").append(count).commit();
        if (count < agencyList.length)
            return false;
        else
            return true;
    }
    
    // override base on needs
    function init(address systemAddr) internal {}

    // override base on needs
    function beforeAdd(bytes32 agency) internal returns (bool) {
        return true;
    }

    // override base on needs
    function beforeDel(bytes32 agency) internal returns (bool) {
        return true;
    }
}