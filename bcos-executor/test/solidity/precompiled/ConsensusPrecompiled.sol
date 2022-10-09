// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;

contract ConsensusPrecompiled {
    function addSealer(string memory,uint256) public returns (int32){}
    function addObserver(string memory) public returns (int32){}
    function remove(string memory) public returns (int32){}
    function setWeight(string memory,uint256) public returns (int32){}
}
