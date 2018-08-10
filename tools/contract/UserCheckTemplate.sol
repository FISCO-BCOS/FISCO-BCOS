pragma solidity ^0.4.4;
import "UserCheck.sol";

contract UserCheckTemplate{
	function newUserCheckContract(address[] userForbidden) public returns (address)
	{
		UserCheck userCheck = new UserCheck();
		userCheck.addUser(userForbidden);
		return userCheck;
	}
}