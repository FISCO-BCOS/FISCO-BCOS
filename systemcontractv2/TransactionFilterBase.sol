pragma solidity ^0.4.4;

contract TransactionFilterBase {
    string public name;
    string  public   version;
    function process(address origin, address from, address to, string func, string input) public constant returns(bool);

    function setName(string n) public constant {
    	name=n;
    }
    function setVersion(string v) public constant {
    	version=v;
    }
}