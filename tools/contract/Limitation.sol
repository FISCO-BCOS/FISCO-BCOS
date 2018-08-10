pragma solidity ^0.4.4;

contract Limitation {
	mapping(address=>uint256) private accountMapToSpent;
	mapping(address=>uint256) private accountMapToLimit;

	function setLimitation(address account, uint256 limit) public {
		accountMapToLimit[account] = limit;
	}
	
	function getLimit(address account) constant public returns(uint256) {
		return accountMapToLimit[account];
	}

	function checkSpent(address account, uint256 limit) constant public returns(bool) {
		return (accountMapToSpent[account] + limit <= accountMapToLimit[account]);
	}

	function addSpent(address account, uint256 limit) public returns(bool) {
		if (checkSpent(account, limit) == false) {
			return false;
		} else {
			accountMapToSpent[account] = accountMapToSpent[account] + limit;
			return true;
		}
	}
	
	function getSpent(address account) constant public returns(uint256) {
		return accountMapToSpent[account];
	}
}