pragma solidity ^0.4.4;

contract TransactionFilterBase {
    string public _name;
    string public _version;
    string public _desc;
    function process(address origin, address from, address to, string func, string input) public constant returns(bool);
    function deploy(address origin) public constant returns(bool);

    function setName(string name) public {
    	_name = name;
    }
    function setVersion(string version) public {
    	_version = version;
    }
	function setDesc(string desc) public {
    	_desc = desc;
    }
}