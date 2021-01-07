contract GasChargeManagePrecompiled
{
    function charge(address userAccount, uint256 gasValue) public returns(int256, uint256){}
    function deduct(address userAccount, uint256 gasValue) public returns(int256, uint256){}
    function queryRemainGas(address userAccount) public view returns(int256, uint256){}

    function grantCharger(address chargerAccount) public returns(int256){}
    function revokeCharger(address chargerAccount) public returns(int256){}
    function listChargers() public view returns(address[]){}
}