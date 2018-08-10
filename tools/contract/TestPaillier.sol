pragma solidity ^0.4.2;

import "LibPaillier.sol";

contract TestPaillier {

    function add(string d1, string d2) public constant returns (string) {
		return LibPaillier.paillierAdd(d1, d2);
    }
	
	function put() public constant returns (string) {
		return "Hellp";
    }
}
