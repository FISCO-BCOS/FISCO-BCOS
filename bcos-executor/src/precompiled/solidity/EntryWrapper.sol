// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;
import "./Cast.sol";

// 记录，用于select和insert
struct Entry {
    string key;
    string[] fields; // 考虑2.0的Entry接口，临时Precompiled的问题，考虑加工具类接口
}

contract EntryWrapper {   
    Cast constant cast =  Cast(address(0x100f));  
    Entry entry;
    constructor(Entry memory _entry) public {
        entry = _entry;
    }
    function setEntry(Entry memory _entry) public {
        entry = _entry;
    }
    function getEntry() public view returns(Entry memory) {
        return entry;
    }
    function fieldSize() public view returns (uint256) {
        return entry.fields.length;
    }
    function getInt(uint256 idx) public view returns (int256) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        return cast.stringToS256(entry.fields[idx]);
    }
    function getUInt(uint256 idx) public view returns (uint256) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        return cast.stringToU256(entry.fields[idx]);
    }
    function getAddress(uint256 idx) public view returns (address) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        return cast.stringToAddr(entry.fields[idx]);
    }
    function getBytes64(uint256 idx) public view returns (bytes1[64] memory) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        return bytesToBytes64(bytes(entry.fields[idx]));
    }
    function getBytes32(uint256 idx) public view returns (bytes32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        return cast.stringToBytes32(entry.fields[idx]);
    }
    function getString(uint256 idx) public view returns (string memory) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        return entry.fields[idx];
    }
    function set(uint256 idx, int256 value) public returns(int32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        entry.fields[idx] = cast.s256ToString(value);
        return 0;
    }
    function set(uint256 idx, uint256 value) public returns(int32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        entry.fields[idx] = cast.u256ToString(value);
        return 0;
    }
    function set(uint256 idx, string memory value) public returns(int32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        entry.fields[idx] = value;
        return 0;
    }
    function set(uint256 idx, address value) public returns(int32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        entry.fields[idx] = cast.addrToString(value);
        return 0;
    }
    function set(uint256 idx, bytes32 value) public returns(int32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        entry.fields[idx] = cast.bytes32ToString(value);
        return 0;
    }
    function set(uint256 idx, bytes1[64] memory value) public returns(int32) {
        require(idx >= 0 && idx < fieldSize(), "Index out of range!");
        entry.fields[idx] = string(bytes64ToBytes(value));
        return 0;
    }
    function setKey(string memory value) public {
        entry.key = value;
    }
    function getKey() public view returns (string memory) {
        return entry.key;
    }
    function bytes64ToBytes(bytes1[64] memory src) private pure returns(bytes memory) {
        bytes memory dst = new bytes(64);
        for(uint32 i = 0; i < 64; i++) {
            dst[i] = src[i][0];
        }
        return dst;
    }
    function bytesToBytes64(bytes memory src) private pure returns(bytes1[64] memory) {
        bytes1[64] memory dst;
        for(uint32 i = 0; i < 64; i++) {
            dst[i] = src[i];
        }
        return dst;
    }
}
