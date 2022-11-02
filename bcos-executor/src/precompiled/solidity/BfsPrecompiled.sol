// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

    struct BfsInfo {
        string file_name;
        string file_type;
        string[] ext;
    }

abstract contract BfsPrecompiled {
    // @return return BfsInfo at most 500, if you want more, try list with paging interface
    function list(string memory absolutePath) public view returns (int32, BfsInfo[] memory);
    // @return int, >=0 -> BfsInfo left, <0 -> errorCode
    function list(string memory absolutePath, uint offset, uint limit) public view returns (int, BfsInfo[] memory);

    function mkdir(string memory absolutePath) public returns (int32);

    function link(string memory absolutePath, address _address, string memory _abi) public returns (int);
    // for cns compatibility
    function link(string memory name, string memory version, address _address, string memory _abi) public returns (int32);

    function readlink(string memory absolutePath) public view returns (address);

    function touch(string memory absolutePath, string memory fileType) public returns (int32);

    function rebuildBfs() public returns (int);
}