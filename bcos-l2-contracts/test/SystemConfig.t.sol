// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import "forge-std/Test.sol";
import {SystemConfig} from "../src/SystemConfig.sol";
import {ISystemConfig} from "../src/interfaces/ISystemConfig.sol";

contract SystemConfigTest is Test {
    SystemConfig cfg;
    address owner = address(0xCAFE);

    function setUp() public {
        vm.prank(owner);
        cfg = new SystemConfig(901, 30_000_000, 1, 0, owner);
    }

    function test_GetChainConfig_ReturnsConstructorValues() public view {
        (uint256 chainId, uint64 gasLimit, uint32 version, uint256 flags) = cfg.getChainConfig();
        assertEq(chainId, 901);
        assertEq(gasLimit, 30_000_000);
        assertEq(version, 1);
        assertEq(flags, 0);
    }

    function test_SetFeatureFlag_OnlyOwner() public {
        vm.prank(address(0xBEEF));
        vm.expectRevert("not owner");
        cfg.setFeatureFlag(1);
        vm.prank(owner);
        cfg.setFeatureFlag(0x42);
        (,,, uint256 flags) = cfg.getChainConfig();
        assertEq(flags, 0x42);
    }

    function test_SetHardforkActivation_OnlyOwner() public {
        vm.prank(owner);
        cfg.setHardforkActivation(4, 1_700_000_000);
        assertEq(cfg.getHardforkActivation(4), 1_700_000_000);
    }

    function test_TransferOwnership_NonOwnerReverts() public {
        vm.prank(address(0xBEEF));
        vm.expectRevert("not owner");
        cfg.transferOwnership(address(0xD00D));
    }

    function test_TransferOwnership_ZeroAddressReverts() public {
        vm.prank(owner);
        vm.expectRevert("zero owner");
        cfg.transferOwnership(address(0));
    }

    function test_TransferOwnership_NewOwnerTakesOver() public {
        address newOwner = address(0xD00D);
        vm.prank(owner);
        cfg.transferOwnership(newOwner);
        assertEq(cfg.owner(), newOwner);

        // New owner can now mutate.
        vm.prank(newOwner);
        cfg.setFeatureFlag(0x99);
        (,,, uint256 flags) = cfg.getChainConfig();
        assertEq(flags, 0x99);

        // Old owner is locked out.
        vm.prank(owner);
        vm.expectRevert("not owner");
        cfg.setFeatureFlag(0x1);
    }

    function test_SetFeatureFlag_EmitsOldValueFirst() public {
        // Initial flags are 0 (from setUp). The event must carry the OLD value
        // (0) first and the NEW value (0x42) second, emitted before mutation.
        vm.expectEmit(true, true, true, true);
        emit ISystemConfig.FeatureFlagsUpdated(0, 0x42);
        vm.prank(owner);
        cfg.setFeatureFlag(0x42);
    }
}
