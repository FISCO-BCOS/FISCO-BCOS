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
    address v3 = address(0x3333);

    function _val(address fee, uint64 power, bytes memory key)
        internal
        pure
        returns (IL2ValidatorSet.Validator memory)
    {
        return IL2ValidatorSet.Validator(payable(fee), false, power, key, 0);
    }

    function setUp() public {
        address[] memory addrs = new address[](1);
        addrs[0] = v1;
        IL2ValidatorSet.Validator[] memory vals = new IL2ValidatorSet.Validator[](1);
        vals[0] = _val(v1, 1000, hex"01");
        vs = new L2ValidatorSet();
        vs.initialize(addrs, vals, owner);
    }

    function test_Initialize_InstallsValidators() public view {
        address[] memory got = vs.getValidators();
        assertEq(got.length, 1);
        assertEq(got[0], v1);
        assertTrue(vs.isValidator(v1));
        assertFalse(vs.isValidator(v2));
    }

    function test_GetValidator_ReturnsRecord() public view {
        IL2ValidatorSet.Validator memory val = vs.getValidator(v1);
        assertEq(val.feeAddress, v1);
        assertEq(val.votingPower, 1000);
        assertEq(val.consensusPublicKey, hex"01");
        assertFalse(val.jailed);
        assertEq(val.incoming, 0);
    }

    function test_AddValidator_OnlyOwner() public {
        IL2ValidatorSet.Validator memory v = _val(v2, 2000, hex"02");
        vm.prank(address(0xBEEF));
        vm.expectRevert("Ownable: caller is not the owner");
        vs.addValidator(v2, v);

        vm.prank(owner);
        vs.addValidator(v2, v);
        assertTrue(vs.isValidator(v2));
        assertEq(vs.getValidators().length, 2);
    }

    function test_AddValidator_DuplicateReverts() public {
        vm.prank(owner);
        vm.expectRevert("duplicate validator");
        vs.addValidator(v1, _val(v1, 1, hex"01"));
    }

    function test_AddValidator_ZeroAddressReverts() public {
        vm.prank(owner);
        vm.expectRevert("zero address");
        vs.addValidator(address(0), _val(address(0), 1, hex"01"));
    }

    function test_RemoveValidator_OnlyOwnerAndRemoves() public {
        vm.prank(address(0xBEEF));
        vm.expectRevert("Ownable: caller is not the owner");
        vs.removeValidator(v1);

        vm.prank(owner);
        vs.removeValidator(v1);
        assertFalse(vs.isValidator(v1));
        assertEq(vs.getValidators().length, 0);
    }

    function test_RemoveValidator_NotValidatorReverts() public {
        vm.prank(owner);
        vm.expectRevert("not a validator");
        vs.removeValidator(v2);
    }

    /// Remove the first of three; the swap-pop must keep the other two consistent
    /// (still present, still removable — i.e. their _indexPlus1 stays correct).
    function test_RemoveValidator_SwapPopKeepsOthers() public {
        vm.startPrank(owner);
        vs.addValidator(v2, _val(v2, 2000, hex"02"));
        vs.addValidator(v3, _val(v3, 3000, hex"03"));
        vs.removeValidator(v1); // v1 was index 0; v3 (last) swaps into its place
        vm.stopPrank();

        assertEq(vs.getValidators().length, 2);
        assertTrue(vs.isValidator(v2));
        assertTrue(vs.isValidator(v3));
        assertFalse(vs.isValidator(v1));

        // Both remaining must still be removable (proves indices were fixed up).
        vm.startPrank(owner);
        vs.removeValidator(v3);
        vs.removeValidator(v2);
        vm.stopPrank();
        assertEq(vs.getValidators().length, 0);
    }

    function test_UpdateValidator_OnlyOwnerAndUpdates() public {
        IL2ValidatorSet.Validator memory upd = _val(v2, 9999, hex"aabb");
        vm.prank(address(0xBEEF));
        vm.expectRevert("Ownable: caller is not the owner");
        vs.updateValidator(v1, upd);

        vm.prank(owner);
        vs.updateValidator(v1, upd);
        IL2ValidatorSet.Validator memory got = vs.getValidator(v1);
        assertEq(got.votingPower, 9999);
        assertEq(got.feeAddress, v2);
        assertEq(got.consensusPublicKey, hex"aabb");
        // still in the set, length unchanged
        assertEq(vs.getValidators().length, 1);
        assertTrue(vs.isValidator(v1));
    }

    function test_UpdateValidator_NotValidatorReverts() public {
        vm.prank(owner);
        vm.expectRevert("not a validator");
        vs.updateValidator(v2, _val(v2, 1, hex"02"));
    }

    function test_GetCurrentValidators_AddrsAndKeys() public {
        vm.prank(owner);
        vs.addValidator(v2, _val(v2, 2000, hex"bbbb"));
        (address[] memory addrs, bytes[] memory keys) = vs.getCurrentValidators();
        assertEq(addrs.length, 2);
        assertEq(keys.length, 2);
        assertEq(addrs[0], v1);
        assertEq(addrs[1], v2);
        assertEq(keys[0], hex"01");
        assertEq(keys[1], hex"bbbb");
    }

    /// Packed slot (feeAddress|jailed|votingPower): setting each to a boundary
    /// value must not bleed into the neighbours.
    function test_Packing_FieldsIndependent() public {
        address fee = address(type(uint160).max);
        IL2ValidatorSet.Validator memory v =
            IL2ValidatorSet.Validator(payable(fee), true, type(uint64).max, hex"ff", 0);
        vm.prank(owner);
        vs.addValidator(v2, v);
        IL2ValidatorSet.Validator memory got = vs.getValidator(v2);
        assertEq(got.feeAddress, fee);
        assertTrue(got.jailed);
        assertEq(got.votingPower, type(uint64).max);
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

    function test_Initialize_CannotBeCalledTwice() public {
        address[] memory addrs = new address[](0);
        IL2ValidatorSet.Validator[] memory vals = new IL2ValidatorSet.Validator[](0);
        vm.expectRevert("Initializable: contract is already initialized");
        vs.initialize(addrs, vals, address(0xD00D));
    }

    function test_Initialize_LengthMismatchReverts() public {
        address[] memory addrs = new address[](2);
        IL2ValidatorSet.Validator[] memory vals = new IL2ValidatorSet.Validator[](1);
        L2ValidatorSet fresh = new L2ValidatorSet();
        vm.expectRevert("length mismatch");
        fresh.initialize(addrs, vals, owner);
    }
}
