// RIP-7212 secp256r1 P256VERIFY (0x0100) precompile vectors (Phase 2 line B,
// Task 3). Four cases spanning the p256Verify semantics matrix:
//
//	fjord_precompile_p256_valid     valid signature (160B) -> 32-byte-1, precompile gas 3450
//	fjord_precompile_p256_invalid   wrong r (160B)         -> empty-output SUCCESS, precompile gas 3450
//	fjord_precompile_p256_wronglen  159B                   -> empty-output SUCCESS, precompile gas 3450
//	ecotone_precompile_p256_noop    valid sig, pre-Fjord   -> no-op, full gas, empty output
//
// p256Verify.Run (op-geth core/vm/contracts.go:1717-1733): requires exactly
// 160 bytes; wrong length OR invalid signature returns (nil, nil) = empty
// SUCCESS; a valid signature returns 32-byte-1. FISCO p256verify_execute
// (bcos-evm/eth/state/precompiles.cpp:753-775) matches. At ecotone the
// precompile is not yet in the active set (Fjord is the first OP fork to bind
// 0x0100, core/vm/contracts.go:182-193; activePrecompiledContracts falls back
// to PrecompiledContractsCancun), so the CALL hits an empty-code account:
// no-op success, full gas returned, empty output.
package main

// invalidP256Sig returns a structurally valid 160B P256VERIFY input with a
// WRONG signature: the valid sig's r with its first byte flipped (0x1a ->
// 0x1b). r stays in [1, N-1] but no longer satisfies the ECDSA equation, so
// secp256r1.Verify returns false and p256Verify.Run returns (nil, nil) =
// empty-output success.
func invalidP256Sig() []byte {
	in := append([]byte(nil), validP256Sig()...)
	in[32] ^= 0x01
	return in
}

// wrongLenP256Sig is the valid signature truncated to 159 bytes -- one byte
// short of p256Verify's 160B requirement (len != 160 -> (nil, nil)).
var wrongLenP256Sig = validP256Sig()[:159]

func init() {
	caseSpecs = append(caseSpecs,
		precompileFrame("fjord", "precompile_p256_valid",
			"RIP-7212 P256VERIFY (0x0100): valid secp256r1 signature (160B = hash||r||s||x||y, validP256Sig) -> success, 32-byte-1 output; precompile gas 3450",
			[]inputTx{precompileCallTx(addrBytes(preP256Verify), validP256Sig(), 500_000, 0)},
			10_000_000),
		precompileFrame("fjord", "precompile_p256_invalid",
			"RIP-7212 P256VERIFY (0x0100): structurally valid 160B input with WRONG r (first byte of valid r flipped) -> empty-output success (nil,nil); precompile gas 3450",
			[]inputTx{precompileCallTx(addrBytes(preP256Verify), invalidP256Sig(), 500_000, 0)},
			10_000_000),
		precompileFrame("fjord", "precompile_p256_wronglen",
			"RIP-7212 P256VERIFY (0x0100): 159B input (valid signature truncated by 1B) -> empty-output success (len != 160 -> nil,nil); precompile gas 3450",
			[]inputTx{precompileCallTx(addrBytes(preP256Verify), wrongLenP256Sig, 500_000, 0)},
			10_000_000),
		precompileFrame("ecotone", "precompile_p256_noop",
			"RIP-7212 P256VERIFY (0x0100) BEFORE Fjord activation (ecotone): 0x0100 has no code -> no-op success, full gas returned, empty output (no 3450 charged)",
			[]inputTx{precompileCallTx(addrBytes(preP256Verify), validP256Sig(), 500_000, 0)},
			10_000_000),
	)
}
