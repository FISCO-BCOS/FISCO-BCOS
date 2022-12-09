pragma solidity ^0.4.25;

import "./precompiled/ConsensusPrecompiled.sol";
import "./precompiled/SystemConfigPrecompiled.sol";
import "./precompiled/ParallelConfigPrecompiled.sol";

contract TestConsensusPrecompiled {
    ConsensusPrecompiled consensus;
    constructor () public {
        consensus = ConsensusPrecompiled(0x1003);
    }

    function addSealerTest(string nodeId, uint256 weight) returns (int256){
        return consensus.addSealer(nodeId, weight);
    }

    function addObserverTest(string nodeId) returns (int256){
        return consensus.addObserver(nodeId);
    }

    function removeTest(string nodeId) returns (int256){
        return consensus.remove(nodeId);
    }

    function setWeightTest(string nodeId, uint256 weight) returns (int256){
        return consensus.setWeight(nodeId, weight);
    }
}

contract TestSysConfig {
    SystemConfigPrecompiled sys;
    constructor () public {
        sys = SystemConfigPrecompiled(0x1000);
    }
    function setValueByKeyTest(string key, string value) public returns (int256){
        return sys.setValueByKey(key, value);
    }

    function getValueByKeyTest(string key) public returns (string, int256){
        return sys.getValueByKey(key);
    }
}

contract TestParaConfig {
    ParallelConfigPrecompiled parallel;
    constructor() public {
        parallel = ParallelConfigPrecompiled(0x1006);
    }
    function registerParallelFunctionInternal(address selector, string func, uint256 size) public returns (int256){
        return parallel.registerParallelFunctionInternal(selector, func, size);
    }

    function unregisterParallelFunctionInternal(address selector, string func) public returns (int256){
        return parallel.unregisterParallelFunctionInternal(selector, func);
    }
}