pragma solidity ^0.4.24;

contract ContractStatusPrecompiled {
    function kill(address addr) public returns (int256);
    function freeze(address addr) public returns (int256);
    function unfreeze(address addr) public returns (int256);
    function queryStatus(address addr) public view returns (uint256, string);
}
