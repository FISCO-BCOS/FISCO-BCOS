// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import "forge-std/Test.sol";
import {L2ValidatorSet} from "../src/L2ValidatorSet.sol";
import {IL2ValidatorSet} from "../src/interfaces/IL2ValidatorSet.sol";

contract L2ValidatorSetTest is Test {
    L2ValidatorSet vs;
    address owner = address(0xCAFE);
    address v1 = address(0x1111);
    address v2 = address(0x2222);

    function setUp() public {
        IL2ValidatorSet.Validator[] memory initial = new IL2ValidatorSet.Validator[](1);
        initial[0] = IL2ValidatorSet.Validator(v1, payable(v1), hex"01", 1000, false, 0);
        vm.prank(owner);
        vs = new L2ValidatorSet(initial, owner);
    }

    function test_GetValidators_ReturnsInitial() public view {
        address[] memory addrs = vs.getValidators();
        assertEq(addrs.length, 1);
        assertEq(addrs[0], v1);
    }

    function test_IsCurrentValidator() public view {
        assertTrue(vs.isCurrentValidator(v1));
        assertFalse(vs.isCurrentValidator(v2));
    }

    function test_UpdateValidatorSet_OnlyOwner() public {
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](1);
        next[0] = IL2ValidatorSet.Validator(v2, payable(v2), hex"02", 2000, false, 0);
        vm.prank(address(0xBEEF));
        vm.expectRevert("not owner");
        vs.updateValidatorSet(next);
        vm.prank(owner);
        vs.updateValidatorSet(next);
        assertTrue(vs.isCurrentValidator(v2));
        assertFalse(vs.isCurrentValidator(v1));
    }

    function test_PhaseA_JailedAlwaysFalse() public view {
        IL2ValidatorSet.Validator memory val = vs.getValidator(v1);
        assertFalse(val.jailed);
        assertEq(val.incoming, 0);
    }

    function test_UpdateValidatorSet_DuplicateReverts() public {
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](2);
        next[0] = IL2ValidatorSet.Validator(v2, payable(v2), hex"02", 2000, false, 0);
        next[1] = IL2ValidatorSet.Validator(v2, payable(v2), hex"03", 3000, false, 0);
        vm.prank(owner);
        vm.expectRevert("duplicate validator");
        vs.updateValidatorSet(next);
    }

    function test_UpdateValidatorSet_EmptyRemovesAllAndBumpsEpoch() public {
        uint256 epochBefore = vs.epoch();
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](0);
        vm.prank(owner);
        vs.updateValidatorSet(next);

        assertFalse(vs.isCurrentValidator(v1));
        assertEq(vs.getValidators().length, 0);
        assertEq(vs.epoch(), epochBefore + 1);
    }

    function test_GetCurrentValidators_ReturnsAddrsAndBlsKeys() public {
        // Install a 2-validator set with distinct BLS keys.
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](2);
        next[0] = IL2ValidatorSet.Validator(v1, payable(v1), hex"aa", 1000, false, 0);
        next[1] = IL2ValidatorSet.Validator(v2, payable(v2), hex"bbbb", 2000, false, 0);
        vm.prank(owner);
        vs.updateValidatorSet(next);

        (address[] memory addrs, bytes[] memory bls) = vs.getCurrentValidators();
        assertEq(addrs.length, 2);
        assertEq(bls.length, 2);
        assertEq(addrs[0], v1);
        assertEq(addrs[1], v2);
        assertEq(bls[0], hex"aa");
        assertEq(bls[1], hex"bbbb");
    }

    function test_Felony_NotImplemented() public {
        vm.prank(owner);
        vm.expectRevert("not implemented in Phase A");
        vs.felony(v1);
    }

    function test_Misdemeanor_NotImplemented() public {
        vm.prank(owner);
        vm.expectRevert("not implemented in Phase A");
        vs.misdemeanor(v1);
    }

    function test_UpdateValidatorSet_TooLargeReverts() public {
        uint256 n = vs.MAX_VALIDATORS() + 1;
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](n);
        for (uint256 i = 0; i < n; i++) {
            address a = address(uint160(i + 1));
            next[i] = IL2ValidatorSet.Validator(a, payable(a), hex"01", 1, false, 0);
        }
        vm.prank(owner);
        vm.expectRevert("validator set too large");
        vs.updateValidatorSet(next);
    }

    function test_UpdateValidatorSet_EmitsEvent() public {
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](1);
        next[0] = IL2ValidatorSet.Validator(v2, payable(v2), hex"02", 2000, false, 0);
        address[] memory expected = new address[](1);
        expected[0] = v2;

        // setUp's constructor already bumped epoch to 1, so this update is epoch 2.
        vm.expectEmit(true, true, true, true);
        emit IL2ValidatorSet.ValidatorSetUpdated(vs.epoch() + 1, expected);
        vm.prank(owner);
        vs.updateValidatorSet(next);
    }

    function test_TransferOwnership_NonOwnerRevertsAndNewOwnerTakesOver() public {
        address newOwner = address(0xD00D);

        vm.prank(address(0xBEEF));
        vm.expectRevert("not owner");
        vs.transferOwnership(newOwner);

        vm.prank(owner);
        vs.transferOwnership(newOwner);
        assertEq(vs.owner(), newOwner);

        // New owner can update the set.
        IL2ValidatorSet.Validator[] memory next = new IL2ValidatorSet.Validator[](1);
        next[0] = IL2ValidatorSet.Validator(v2, payable(v2), hex"02", 2000, false, 0);
        vm.prank(newOwner);
        vs.updateValidatorSet(next);
        assertTrue(vs.isCurrentValidator(v2));
    }
}
