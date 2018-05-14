pragma solidity ^0.4.11;

contract ConsensusControlMgr {
    address private consensusControlAddr;

    function ConsensusControlMgr() {
        consensusControlAddr = 0x0;
    }

    function getAddr() external constant returns (address) {
        return consensusControlAddr;
    }

    function setAddr(address _addr) external {
        consensusControlAddr = _addr;
    }

    function turnoff() external {
        consensusControlAddr = 0x0;
    }
}