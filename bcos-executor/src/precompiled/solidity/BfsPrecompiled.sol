// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

    struct BfsInfo{
        string file_name;
        string file_type;
        string[] ext;
    }

contract BfsPrecompiled {
    function list(string memory absolutePath) public view returns (int256,BfsInfo[] memory){}
    function mkdir(string memory absolutePath) public returns (int256){}
    function link(string memory name, string memory version, address _address, string memory _abi) public returns (int256){}
    function readlink(string memory absolutePath) public view returns (address) {}
    function touch(string memory absolutePath, string memory fileType) public returns (int256){}
}