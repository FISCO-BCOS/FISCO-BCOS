// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

    struct BfsInfo{
        string file_name;
        string file_type;
        string[] ext;
    }

contract BfsPrecompiled {
    function list(string memory absolutPath) public returns (int256,BfsInfo[] memory){}
    function mkdir(string memory absolutPath) public returns (int256){}
    function link(string memory name, string memory version, address _address, string memory _abi) public returns (int256){}
}