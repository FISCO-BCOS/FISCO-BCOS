pragma solidity ^0.4.4;

contract UserCheck{
	address[] private _userList;

	function UserCheck() public {
	}
	
	function addUser(address[] userList) public returns (address[]) {
		for (uint256 i = 0; i < userList.length; i++)
		{
			_userList.push(userList[i]);
		}
		return _userList;
	}
	
	function check(address user) public constant returns (bool)
	{
		if (tx.origin != user)
		{
			return false;
		}
		for (uint256 i = 0; i < _userList.length; i++)
		{
			if (user == _userList[i])
			{
				return true;
			}
		}
		return false;
	}
	
	function listUser() public constant returns (address[])
	{
		return _userList;
	}
}