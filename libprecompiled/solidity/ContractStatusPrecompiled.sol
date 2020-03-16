pragma solidity ^0.4.24;

contract ContractStatusPrecompiled {
    function destroy(address addr) public returns(int);
    function freeze(address addr) public returns(int);
    function unfreeze(address addr) public returns(int);
    function grantManager(address contractAddr, address userAddr) public returns(int);
    function getStatus(address addr) public constant returns(int,string);
    function listManager(address addr) public constant returns(int,address[]);
}
