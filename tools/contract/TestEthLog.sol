pragma solidity ^0.4.2;

import "LibPaillier.sol";
import "LibEthLog.sol";

contract TestEthLog {
    using LibEthLog for *;

    function add(string d1, string d2) public constant returns (string) {
        LibEthLog.DEBUG().append("ethcall paillier d1 ").append(d1).commit();
        LibEthLog.DEBUG().append("ethcall paillier d2 ").append(d2).commit();
        string memory res = LibPaillier.paillierAdd(d1, d2);
        LibEthLog.DEBUG().append("ethcall paillier res ").append(res).commit();
        LibEthLog.DEBUG().append("ethcall aaaa\n")
                .append("ethcall bbbb\t")
                .append("ethcall cccc")
                .commit();
		return res;
    }
	
	function put() public constant returns (string) {
		return "Hellp";
    }
}
