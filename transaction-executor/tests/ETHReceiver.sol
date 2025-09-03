// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts-upgradeable/proxy/utils/Initializable.sol";
import "@openzeppelin/contracts-upgradeable/proxy/utils/UUPSUpgradeable.sol";
import "@openzeppelin/contracts-upgradeable/access/OwnableUpgradeable.sol";

contract ETHReceiverV1 is Initializable, UUPSUpgradeable, OwnableUpgradeable {
    uint256 public totalETH; // 存储接收的 ETH 总量

    /// @custom:oz-upgrades-unsafe-allow constructor
    constructor() payable {
        totalETH += msg.value;
        _disableInitializers(); // 禁止外部初始化
    }

    function initialize(address initialOwner) public initializer {
        __Ownable_init(initialOwner); // 初始化所有权
        __UUPSUpgradeable_init(); // 初始化 UUPS 升级功能
    }

    // 接收 ETH 的函数（外部调用）
    function transfer() external payable {
        totalETH += msg.value; // 累加 ETH 余额
    }

    // 升级权限控制（仅合约所有者可调用）
    function _authorizeUpgrade(address) internal override onlyOwner {}

    // 允许合约直接接收 ETH
    receive() external payable {
        totalETH += msg.value;
    }
}