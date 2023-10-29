pragma solidity ^0.4.25;

import "./ParallelContract.sol";

contract EquipmentPurchase is ParallelContract {
    // 合约实现
    mapping(string => uint256)  userBalances;
    mapping(string => uint256)  equipmentInventories;
    uint256 public equipmentPrice = 10;

    function deposit(string memory username,  uint256 num) public payable {
        userBalances[username] += num;
    }

    function buyEquipment(string memory username, uint256 quantity) public {
        uint256 cost = equipmentPrice * quantity;
        require(userBalances[username] >= cost, "Insufficient balance");
        userBalances[username] -= cost;
        equipmentInventories[username] += quantity;
    }

    function getUserBalance(string memory username) public view returns (uint256) {
        return userBalances[username];
    }

    function getEquipmentInventory(string memory username) public view returns (uint256) {
        return equipmentInventories[username];
    }

    // 注册可以并行的合约接口
    function enableParallel() public {
        registerParallelFunction("deposit(string,uint256)", 2);
        registerParallelFunction("buyEquipment(string,uint256)", 2);
    }

    // 注销并行合约接口
    function disableParallel() public {
        unregisterParallelFunction("deposit(string,uint256)");
        unregisterParallelFunction("buyEquipment(string,uint256)");
    }
}
