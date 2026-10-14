// Vendored for the native CI suite (tools/.ci/scenarios, scenario 10). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk
// Path: src/test/resources/contracts/ComplexSol.sol
// Commit: e5ec92365d59298e9dd95ca7563f335c3556f9ac
// License: Apache-2.0 (the source repository's LICENSE; the SPDX line below is upstream's)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

contract ComplexSol{

    uint256 private _uint256V;
    int public _intV;
    address public _addr;
    string public _s;
    bytes public _bytesV;
    bytes4 public _bytes4V;
    uint256[2] _uint8SArray;
    address[] public _addrDArray;
    mapping(bytes => bytes[]) _bytesMapping;

    event LogIncrement(address sender, uint256 a);
    event LogInit(address sender, string s);
    event LogSetValues(int i, address[] a, string s);
    event LogSetBytes(bytes o, bytes b);
    event LogSetSArray(uint256[2] o, uint256[2] n);

    constructor(int i, string memory s) public {
        _addr = msg.sender;
        _intV = i;
        _s = s;
        emit LogInit(msg.sender, s);
    }

    function emptyArgs() public {}


    function incrementUint256(uint256 v) public returns(uint256){
        _uint256V = v + 1 ;
        emit LogIncrement(msg.sender, v);
        return _uint256V;
    }

    function getUint256() public view returns(uint256){
        return _uint256V;
    }

    function setValues(int i, address[] memory a, string memory s) public {
        _intV = i;
        _addrDArray = a;
        _s = s;
        emit LogSetValues(i, a, s);
    }

    function getValues() public view returns (int, address[] memory, string memory) {
        return (_intV,_addrDArray,_s);
    }

    function setBytes(bytes memory b) public returns (bytes memory) {
        emit LogSetBytes(_bytesV, b);
        _bytesV = b;
        return b;
    }

    function setStaticByte4(bytes4 _b) public returns (bool) {
        _bytes4V = _b;
        return true;
    }

    function setBytesMapping(bytes[] memory bytesArray) public returns (bool) {
        require(bytesArray.length>1, "Bytes array is less than 2");
        _bytesMapping[bytesArray[0]] = bytesArray;
        return true;
    }

    function getByBytes(bytes memory b) public view returns (bytes[] memory) {
        return _bytesMapping[b];
    }

    function getSArray() public returns (uint256[2] memory){
        uint256[2] memory arr = [uint256(1),2];
        emit LogSetSArray(arr, _uint8SArray);
        return arr;
    }
}
