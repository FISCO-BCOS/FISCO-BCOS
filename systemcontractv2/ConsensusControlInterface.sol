pragma solidity ^0.4.11;

interface ConsensusControlInterface {

    function addNode(bytes32 agency) external returns (bool);

    function delNode(bytes32 agency) external returns (bool);

    function control(bytes32[] agencyList, uint[] num) external constant returns (bool);
    
}