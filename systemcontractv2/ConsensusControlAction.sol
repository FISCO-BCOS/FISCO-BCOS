pragma solidity ^0.4.11;

import "ConsensusControlInterface.sol";
import "SystemProxy.sol";
import "NodeAction.sol";
import "StringTool.sol";
import "LibEthLog.sol";

contract ConsensusControlAction is Base, ConsensusControlInterface, StringTool  {
    using LibEthLog for *;

    bytes32[] public agencyList;
    mapping (bytes32 => uint) public agencyCountMap;
    // uint private agencyCount;

    function ConsensusControlAction(address systemAddr) {
        initAgencyCountMap(systemAddr);
        init(systemAddr);
    }

    function initAgencyCountMap(address systemAddr) private {
        if (systemAddr != 0x0) {
            LibEthLog.INFO().append("[ConsensusControl] systemProxy load success!").commit();
            SystemProxy sys = SystemProxy(systemAddr);
            var (addr,,) = sys.getRoute("NodeAction");
            if (addr != 0x0) {
                LibEthLog.INFO().append("[ConsensusControl] NodeAction load success!").commit();
                initAddNode(addr);
            } else {
                LibEthLog.ERROR().append("[ConsensusControl] NodeAction load failed! may be a wrong address").commit();    
            }
            LibEthLog.INFO().append("[ConsensusControl] init agnecy finish. current size:").append(agencyList.length).commit();
        } else {
            LibEthLog.ERROR().append("[ConsensusControl] systemProxy load failed! may be a wrong address").commit();
        }
    }

    function initAddNode(address nodeAddr) private {
        NodeAction action = NodeAction(nodeAddr);
        for (uint i = 0; i < action.getNodeIdsLength(); i++) {
            var agency = action.getNodeAgencyByIdx(i);
            // LibEthLog.INFO().append("[ConsensusControl] agency:").append(bytes32ToString(agency)).commit();
            addNodePri(agency);
        }
    }

    // override base on needs
    function init(address systemAddr) internal {}
    
    // need override
    function control(bytes32[] agencyList, uint[] num) external constant returns (bool) { 
        return true; 
    }

    // override base on needs
    function beforeAdd(bytes32 agency) internal returns (bool) {
        return true;
    }

    // override base on needs
    function beforeDel(bytes32 agency) internal returns (bool) {
        return true;
    }

    function addNodePri(bytes32 agency) private {
        if (agencyCountMap[agency] == 0) {
            agencyCountMap[agency] = 1;
            agencyList.push(agency);
        } else {
            agencyCountMap[agency] += 1;
        }

        LibEthLog.INFO().append("[ConsensusControl](in contract) addNodePri:").append(bytes32ToString(agency))
            .append(" count:").append(agencyCountMap[agency]).commit();
    }

    function addNode(bytes32 agency) external returns (bool) {
        if (!beforeAdd(agency))
            return false;

        addNodePri(agency);
        return true;
    }

    function delNode(bytes32 agency) external returns (bool) {
        if (!beforeDel(agency))
            return false;
        uint t = agencyCountMap[agency];
        if (t == 0) { // 这个 agency 不存在
            return true;
        }
       
        t--;
        if (t > 0) {
            agencyCountMap[agency] = t;
        } else {
            delete agencyCountMap[agency];
            uint i = 0;
            for (; i < agencyList.length; i++) {
                if (agencyList[i] == agency) {
                    break;
                }
            }
            if (i < agencyList.length) {
                for (;i < agencyList.length - 1; i++) {
                    agencyList[i] = agencyList[i+1];
                }
                delete agencyList[agencyList.length - 1];
                agencyList.length--;
            }
        }
        LibEthLog.INFO().append("[ConsensusControl] delNode:").append(bytes32ToString(agency))
            .append(" count:").append(agencyCountMap[agency])
            .commit();
        // for (uint j=0; j < agencyList.length; j++) {
        //     LibEthLog.INFO().append("[ConsensusControl] delNode agencyList:")
        //     .append(bytes32ToString(agencyList[j]))
        //     .commit();
        // }
        return true;
    }
    
    function lookupAgencyCount(bytes32 agency) external constant returns (uint) {
        // var agency =stringToBytes32(_agency);
        return agencyCountMap[agency];
    }

    function lookupAgencyList() external constant returns(bytes32[]) {
        return agencyList;
    }

    function printCurrentStateInLog() external constant returns(bool) {
        LibEthLog.INFO().append("[ConsensusControl]  current agencyList:");
        for (uint j=0; j < agencyList.length; j++) {
            LibEthLog.INFO().append("[ConsensusControl] ")
            .append(j)
            .append(":")
            .append(bytes32ToString(agencyList[j]))
            .append(":")
            .append(agencyCountMap[agencyList[j]])
            .commit();
        }
        return true;
    }
}