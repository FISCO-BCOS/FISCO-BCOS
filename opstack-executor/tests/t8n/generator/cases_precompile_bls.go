// BLS12-381 precompile vectors (Phase 2 line B Task 2): 14 caseSpecs --
// 7 normal (fork=isthmus), 6 cap-over-limit (3 isthmus + 3 jovian), 1 no-op
// (fork=fjord). Registered into the shared caseSpecs table via init(); the
// Task 1 file cases.go owns its own 13 and peer files
// cases_precompile_p256.go / cases_precompile_wrapper.go (Tasks 3/4) own
// theirs -- each init() appends, so the slices never collide.
//
// EIP-2537 UNCOMPRESSED encodings (both sides authoritative; mirrors core/vm
// decodeBLS12381FieldElement / encodePointG1/G2):
//
//	Fp = 64B (16 zero || 48B big-endian; 381-bit field)
//	G1 = 128B (x,y in Fp), G2 = 256B (x,y in Fp2, each c0||c1 in Fp)
//	G1MSM element = G1 128B + scalar 32B = 160B
//	G2MSM element = G2 256B + scalar 32B = 288B
//	pairing pair  = G1 128B + G2 256B = 384B
//	mapG1 input   = Fp 64B, mapG2 input = Fp2 128B
//
// Gas (op-geth params/protocol_params.go:174-181 + core/vm RequiredGas):
//
//	G1Add 375, G1MSM k*12000*discount/1000, G2Add 600,
//	G2MSM k*22500*discount/1000, pairing 37700+32600*k, mapG1 5500, mapG2 23800.
//
// The 6 over-cap inputs are the SMALLEST element-multiple strictly exceeding
// the fork's cap (isthmus 513760/488448/235008, jovian 288960/278784/156672;
// caps are strictly `len > cap`). The cap check lives inside Run AFTER
// RequiredGas is charged (RunPrecompiledContract deducts RequiredGas first),
// so the tx gas limit must clear intrinsic + RequiredGas(over-cap input) --
// otherwise the call OOGs at the gas cost and never reaches the cap check.
package main

import (
	"fmt"
	"math/big"
	"os"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/vm"
	"github.com/ethereum/go-ethereum/params"
)

func init() {
	caseSpecs = append(caseSpecs, blsPrecompileCases()...)
	if os.Getenv("OPT8N_PROBE_BLS") == "1" {
		blsProbeEvidence()
		os.Exit(0)
	}
}

// ---------------------------------------------------------------------
// BLS gas helpers (mirror op-geth core/vm RequiredGas)
// ---------------------------------------------------------------------

const (
	blsG1MulGas = params.Bls12381G1MulGas          // 12000
	blsG2MulGas = params.Bls12381G2MulGas          // 22500
	blsPairBase = params.Bls12381PairingBaseGas    // 37700
	blsPairPerK = params.Bls12381PairingPerPairGas // 32600
	// Discount-table floors: for k >= 128 (the table length) RequiredGas uses
	// the LAST entry (519 G1 / 524 G2). Every over-cap vector has k in the
	// thousands, so the floor applies.
	blsG1Discount = 519
	blsG2Discount = 524
)

// blsG1MSMGas is the G1 MSM RequiredGas for k points at the discount-table
// floor (k >= 128). Mirrors bls12381G1MultiExp.RequiredGas.
func blsG1MSMGas(k int) uint64 {
	if k < 128 {
		panic("blsG1MSMGas: discount floor only valid for k >= 128")
	}
	return uint64(k) * blsG1MulGas * blsG1Discount / 1000
}

// blsG2MSMGas is the G2 MSM RequiredGas for k points at the discount-table
// floor (k >= 128). Mirrors bls12381G2MultiExp.RequiredGas.
func blsG2MSMGas(k int) uint64 {
	if k < 128 {
		panic("blsG2MSMGas: discount floor only valid for k >= 128")
	}
	return uint64(k) * blsG2MulGas * blsG2Discount / 1000
}

// blsPairingGas is the pairing RequiredGas for k pairs.
func blsPairingGas(k int) uint64 {
	return blsPairBase + uint64(k)*blsPairPerK
}

// txIntrinsicGas is the EIP-2028 intrinsic gas for an eip1559 tx carrying the
// given calldata (21000 base + 16/non-zero-byte + 4/zero-byte). OP-stack
// charges this FIRST (core/state_transition.go:563), before the precompile
// call's RequiredGas -- so a cap vector's tx gas must be
// intrinsic + RequiredGas + margin or the call OOGs at the gas cost.
func txIntrinsicGas(data []byte) uint64 {
	var nz, z uint64
	for _, b := range data {
		if b == 0 {
			z++
		} else {
			nz++
		}
	}
	return 21000 + 16*nz + 4*z
}

// ---------------------------------------------------------------------
// EIP-2537 input construction (valid content; see precompile_inputs.go)
// ---------------------------------------------------------------------

// blsScalar2 is the 32-byte big-endian scalar 0x02 used by the MSM vectors.
func blsScalar2() []byte { return common.BigToHash(big.NewInt(2)).Bytes() }

// blsG1MSMInput returns k copies of (valid G1 point || scalar 0x02) = 160B each.
func blsG1MSMInput(k int) []byte {
	elem := append(validBlsG1(), blsScalar2()...)
	if len(elem) != 160 {
		panic("blsG1MSMInput: element length mismatch")
	}
	return repeatedPair(elem, k)
}

// blsG2MSMInput returns k copies of (valid G2 point || scalar 0x02) = 288B each.
func blsG2MSMInput(k int) []byte {
	elem := append(validBlsG2(), blsScalar2()...)
	if len(elem) != 288 {
		panic("blsG2MSMInput: element length mismatch")
	}
	return repeatedPair(elem, k)
}

// blsPairInput returns k copies of (valid G1 || valid G2) = 384B each.
func blsPairInput(k int) []byte {
	pair := append(validBlsG1(), validBlsG2()...)
	if len(pair) != 384 {
		panic("blsPairInput: pair length mismatch")
	}
	return repeatedPair(pair, k)
}

// blsMapG1Input returns one Fp element (64B): the generator G1's x-coordinate
// (validBlsG1()[0:64] IS the 64B Fp-slot encoding, 16 zero || 48B).
func blsMapG1Input() []byte { return validBlsG1()[:64] }

// blsMapG2Input returns one Fp2 element (128B): the generator G2's
// x-coordinate (validBlsG2()[0:128] IS the c0||c1 Fp2 encoding).
func blsMapG2Input() []byte { return validBlsG2()[:128] }

// ---------------------------------------------------------------------
// Case list (14)
// ---------------------------------------------------------------------

func blsPrecompileCases() []caseSpec {
	// ---- Step 1: 7 normal vectors (fork=isthmus, tx gas 500_000) ----
	// basePrecompileCase covers intrinsic + RequiredGas for every input here
	// (max pairing RequiredGas 70,300). Each input is genuinely valid
	// (probe-confirmed below) so the precompile succeeds.
	normal := []caseSpec{
		basePrecompileCase("precompile_bls_g1add",
			"bls12-381 g1 add (0x0b): two valid G1 points (256B input; RequiredGas 375)",
			preBlsG1Add, repeatedPair(validBlsG1(), 2)),
		basePrecompileCase("precompile_bls_g1msm",
			"bls12-381 g1 multi-scalar-mul (0x0c): 2 valid (G1, scalar 0x02) pairs (320B input; RequiredGas 22776)",
			preBlsG1MSM, blsG1MSMInput(2)),
		basePrecompileCase("precompile_bls_g2add",
			"bls12-381 g2 add (0x0d): two valid G2 points (512B input; RequiredGas 600)",
			preBlsG2Add, repeatedPair(validBlsG2(), 2)),
		basePrecompileCase("precompile_bls_g2msm",
			"bls12-381 g2 multi-scalar-mul (0x0e): 2 valid (G2, scalar 0x02) pairs (576B input; RequiredGas 45000)",
			preBlsG2MSM, blsG2MSMInput(2)),
		basePrecompileCase("precompile_bls_pairing",
			"bls12-381 pairing check (0x0f): one valid (G1, G2) pair (384B input; RequiredGas 70300)",
			preBlsPairing, blsPairInput(1)),
		basePrecompileCase("precompile_bls_map_g1",
			"bls12-381 map to G1 (0x10): one Fp element, the generator G1 x-coordinate (64B input; RequiredGas 5500)",
			preBlsMapG1, blsMapG1Input()),
		basePrecompileCase("precompile_bls_map_g2",
			"bls12-381 map to G2 (0x11): one Fp2 element, the generator G2 x-coordinate (128B input; RequiredGas 23800)",
			preBlsMapG2, blsMapG2Input()),
	}

	// ---- Step 2: 6 cap-over-limit vectors ----
	// Each input is the smallest element-multiple strictly exceeding the fork's
	// cap. Run's cap check (len > cap) fires AFTER RequiredGas is charged, so
	// tx gas clears intrinsic + RequiredGas + margin; block gas 30M. Status 0,
	// ALL call gas consumed (RunPrecompiledContract zeroes gas on precompile
	// error -- vm/evm.go:390), so receipt gasUsed == tx gas.
	overcap := []caseSpec{
		blsOvercapCase("precompile_bls_g1msm_overcap", "isthmus", preBlsG1MSM,
			blsG1MSMInput(3212), params.Bls12381G1MulMaxInputSizeIsthmus, blsG1MSMGas(3212)),
		blsOvercapCase("precompile_bls_g2msm_overcap", "isthmus", preBlsG2MSM,
			blsG2MSMInput(1697), params.Bls12381G2MulMaxInputSizeIsthmus, blsG2MSMGas(1697)),
		blsOvercapCase("precompile_bls_pairing_overcap", "isthmus", preBlsPairing,
			blsPairInput(613), params.Bls12381PairingMaxInputSizeIsthmus, blsPairingGas(613)),
		blsOvercapCase("precompile_bls_g1msm_overcap", "jovian", preBlsG1MSM,
			blsG1MSMInput(1807), params.Bls12381G1MulMaxInputSizeJovian, blsG1MSMGas(1807)),
		blsOvercapCase("precompile_bls_g2msm_overcap", "jovian", preBlsG2MSM,
			blsG2MSMInput(969), params.Bls12381G2MulMaxInputSizeJovian, blsG2MSMGas(969)),
		blsOvercapCase("precompile_bls_pairing_overcap", "jovian", preBlsPairing,
			blsPairInput(409), params.Bls12381PairingMaxInputSizeJovian, blsPairingGas(409)),
	}

	// ---- Step 3: fjord no-op (BLS inactive) ----
	// At fjord the BLS precompiles are NOT active (PrecompiledContractsFjord
	// has no 0x0b), so calling 0x0b hits empty code: EVMC_SUCCESS, full call
	// gas refunded, empty output. Receipt status 1 with gasUsed == intrinsic.
	noop := []caseSpec{
		precompileFrame("fjord", "precompile_bls_noop",
			"bls12-381 g1 add at fjord (0x0b, BLS not yet active): empty-code no-op -> status 1, full gas refunded, empty output",
			[]inputTx{precompileCallTx(addrBytes(preBlsG1Add), repeatedPair(validBlsG1(), 2), 500_000, 0)},
			10_000_000),
	}

	out := make([]caseSpec, 0, len(normal)+len(overcap)+len(noop))
	out = append(out, normal...)
	out = append(out, overcap...)
	out = append(out, noop...)
	return out
}

// blsOvercapCase builds one BLS cap-over-limit vector. input is the over-cap
// byte string (len > capBytes); required is the precompile's own RequiredGas
// for that input. tx gas = intrinsic + required + 100_000 margin so the call
// reaches Run's cap check (errBLS12381Max*) instead of OOG-ing at the gas
// cost; block gas 30M (the ~20M-class required gas needs it).
func blsOvercapCase(name, fork string, addr uint, input []byte, capBytes, required uint64) caseSpec {
	gas := txIntrinsicGas(input) + required + 100_000
	desc := fmt.Sprintf(
		"bls12-381 over-cap (%s): %dB input > %dB cap; RequiredGas %d; Run returns the max-size error -> status 0, all call gas consumed",
		name, len(input), capBytes, required)
	return precompileFrame(fork, name, desc,
		[]inputTx{precompileCallTx(addrBytes(addr), input, gas, 0)},
		30_000_000)
}

// ---------------------------------------------------------------------
// Direct precompile-Run probe (OPT8N_PROBE_BLS=1, Task 2 Step 4 evidence)
// ---------------------------------------------------------------------

// blsProbeEvidence feeds every BLS input to the REAL op-geth precompile Run
// from the fork's registry (PrecompiledContractsIsthmus / Jovian -- the
// registries that actually carry the cap variants) and prints acceptance/cap
// evidence. The 6 over-cap inputs MUST return the max-size cap error (proving
// the cap path fires, not a decode error); the 7 normal inputs MUST Run clean.
// Gated by OPT8N_PROBE_BLS=1 so normal generator runs are unaffected.
func blsProbeEvidence() {
	isthmus := vm.PrecompiledContractsIsthmus
	jovian := vm.PrecompiledContractsJovian
	pc := func(m vm.PrecompiledContracts, n uint) vm.PrecompiledContract {
		c, ok := m[common.BytesToAddress(addrBytes(n))]
		if !ok {
			panic(fmt.Sprintf("bls probe: no precompile at 0x%x in registry", n))
		}
		return c
	}

	type check struct {
		label  string
		addr   uint
		pc     vm.PrecompiledContract
		input  []byte
		wantOK bool // true: expect Run success; false: expect a max-size cap error
	}
	checks := []check{
		{"isthmus G1Add", preBlsG1Add, pc(isthmus, preBlsG1Add), repeatedPair(validBlsG1(), 2), true},
		{"isthmus G1MSM k=2", preBlsG1MSM, pc(isthmus, preBlsG1MSM), blsG1MSMInput(2), true},
		{"isthmus G2Add", preBlsG2Add, pc(isthmus, preBlsG2Add), repeatedPair(validBlsG2(), 2), true},
		{"isthmus G2MSM k=2", preBlsG2MSM, pc(isthmus, preBlsG2MSM), blsG2MSMInput(2), true},
		{"isthmus Pairing k=1", preBlsPairing, pc(isthmus, preBlsPairing), blsPairInput(1), true},
		{"isthmus MapG1", preBlsMapG1, pc(isthmus, preBlsMapG1), blsMapG1Input(), true},
		{"isthmus MapG2", preBlsMapG2, pc(isthmus, preBlsMapG2), blsMapG2Input(), true},

		{"isthmus G1MSM k=3212 over-cap", preBlsG1MSM, pc(isthmus, preBlsG1MSM), blsG1MSMInput(3212), false},
		{"isthmus G2MSM k=1697 over-cap", preBlsG2MSM, pc(isthmus, preBlsG2MSM), blsG2MSMInput(1697), false},
		{"isthmus Pairing k=613 over-cap", preBlsPairing, pc(isthmus, preBlsPairing), blsPairInput(613), false},
		{"jovian G1MSM k=1807 over-cap", preBlsG1MSM, pc(jovian, preBlsG1MSM), blsG1MSMInput(1807), false},
		{"jovian G2MSM k=969 over-cap", preBlsG2MSM, pc(jovian, preBlsG2MSM), blsG2MSMInput(969), false},
		{"jovian Pairing k=409 over-cap", preBlsPairing, pc(jovian, preBlsPairing), blsPairInput(409), false},
	}

	// No-op confirmation: at fjord 0x0b is NOT in the fjord registry -> the
	// call hits empty code (no-op success), not a BLS precompile.
	if _, ok := vm.PrecompiledContractsFjord[common.BytesToAddress(addrBytes(preBlsG1Add))]; ok {
		panic("bls probe: 0x0b unexpectedly present in the fjord precompile registry")
	}
	fmt.Printf("  ok    0x0b absent from PrecompiledContractsFjord (empty-code no-op at fjord)\n")

	fail := 0
	for _, c := range checks {
		out, err := c.pc.Run(c.input)
		switch {
		case c.wantOK && err != nil:
			fmt.Printf("  FAIL  %-34s Run error: %v\n", c.label, err)
			fail++
		case !c.wantOK && err == nil:
			fmt.Printf("  FAIL  %-34s expected max-size cap error, got success (out %dB)\n", c.label, len(out))
			fail++
		case c.wantOK:
			fmt.Printf("  ok    %-34s Run ok, output %dB (RequiredGas=%d)\n", c.label, len(out), c.pc.RequiredGas(c.input))
		default:
			fmt.Printf("  ok    %-34s Run err=%q (RequiredGas=%d)\n", c.label, err.Error(), c.pc.RequiredGas(c.input))
		}
	}
	if fail > 0 {
		panic(fmt.Sprintf("blsProbeEvidence: %d check(s) FAILED", fail))
	}
	fmt.Printf("bls probe: all %d checks passed (cap path confirmed for the 6 over-cap inputs)\n", len(checks))
}
