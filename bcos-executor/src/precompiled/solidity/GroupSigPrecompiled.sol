// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;

abstract contract GroupSigPrecompiled {
    function groupSigVerify(string memory, string memory, string memory, string memory) public view returns (int32,bool);
}
