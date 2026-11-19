// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import "forge-std/Test.sol";
import {SystemConfig} from "../src/SystemConfig.sol";
import {TransparentUpgradeableProxy} from
    "@openzeppelin/contracts/proxy/transparent/TransparentUpgradeableProxy.sol";

/// @title ProxyAuthorityTest
/// @notice Proves the two-authority split THROUGH a live proxy: the EIP-1967
///         admin can only upgrade (it cannot reach the implementation's
///         functions at all), while `Ownable.owner` reaches the config path
///         (today an empty whitelist) but cannot upgrade. The genesis overlay
///         materializes exactly this end state
///         (admin slot = ProxyAdmin, owner slot = governance entity), so
///         these tests pin the runtime semantics that state implies.
/// @dev    Uses OZ v4.7 TransparentUpgradeableProxy as the ERC-1967 stand-in;
///         the OP `Proxy.sol` used at genesis implements the same
///         admin-cannot-fallback / owner-cannot-upgrade discipline.
contract ProxyAuthorityTest is Test {
    SystemConfig impl;
    TransparentUpgradeableProxy proxy;
    SystemConfig cfg; // SystemConfig interface over the proxy address

    address admin = address(0xAD);   // upgrade authority (ProxyAdmin stand-in)
    address owner = address(0xCAFE); // governance entity (config authority)

    // A genesis-seeded key; `_config` base slot as pinned by storage-layout/SystemConfig.json.
    string constant GENESIS_KEY = "chain_id";
    uint256 constant CONFIG_BASE_SLOT = 101;

    function setUp() public {
        impl = new SystemConfig();
        proxy = new TransparentUpgradeableProxy(
            address(impl), admin, abi.encodeCall(SystemConfig.initialize, (owner)));
        cfg = SystemConfig(address(proxy));
    }

    /// Genesis writes the Entry slot into the PROXY account (build-allocs.py); the proxy
    /// delegates reads to the implementation. vm.store stands in for the genesis tooling.
    function seedEntry(string memory key, uint192 value, uint64 enableNumber) internal {
        bytes32 slot = keccak256(abi.encodePacked(key, CONFIG_BASE_SLOT));
        vm.store(address(proxy), slot, bytes32((uint256(enableNumber) << 192) | uint256(value)));
    }

    /// The governance owner reaches setValueByKey through the proxy: the call is delegated
    /// and passes onlyOwner, then stops at the (empty) key whitelist, not at the proxy.
    function test_OwnerReachesConfig_ThroughProxy() public {
        vm.prank(owner);
        vm.expectRevert("SystemConfig: key not runtime-writable");
        cfg.setValueByKey(GENESIS_KEY, 1000, 5);
        assertEq(cfg.owner(), owner);
    }

    /// The proxy admin cannot reach setValueByKey at all: a transparent proxy
    /// refuses to delegate any call from the admin.
    function test_AdminCannotWriteConfig() public {
        vm.prank(admin);
        vm.expectRevert("TransparentUpgradeableProxy: admin cannot fallback to proxy target");
        cfg.setValueByKey(GENESIS_KEY, 1, 0);
    }

    /// The governance owner cannot upgrade: its upgradeTo call is delegated
    /// to the implementation, which has no such function.
    function test_OwnerCannotUpgrade() public {
        SystemConfig newImpl = new SystemConfig();
        vm.prank(owner);
        vm.expectRevert();
        TransparentUpgradeableProxy(payable(address(proxy))).upgradeTo(address(newImpl));
    }

    /// The admin upgrades the implementation; proxy storage (owner + config
    /// entries) survives because state lives in the proxy account — exactly
    /// the property the genesis overlay relies on.
    function test_AdminUpgrades_StateSurvives() public {
        seedEntry(GENESIS_KEY, 1000, 5);
        (uint192 v0, uint64 e0) = cfg.getValueByKey(GENESIS_KEY);
        assertEq(uint256(v0), 1000);
        assertEq(uint256(e0), 5);

        SystemConfig newImpl = new SystemConfig();
        vm.prank(admin);
        TransparentUpgradeableProxy(payable(address(proxy))).upgradeTo(address(newImpl));

        (uint192 v, uint64 e) = cfg.getValueByKey(GENESIS_KEY);
        assertEq(uint256(v), 1000);
        assertEq(uint256(e), 5);
        assertEq(cfg.owner(), owner);
    }
}
