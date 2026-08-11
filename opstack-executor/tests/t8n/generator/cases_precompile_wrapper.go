// Wrapper-contract CALL-semantics precompile vectors (Phase 2 line B Task 4).
//
// Five cases exercise the CALL path from a wrapper CONTRACT into a precompile
// (as opposed to the Task-1 direct-call vectors): EIP-150 63/64 gas
// forwarding, returndata-size truncation, value-bearing revert propagation,
// the OpHost cap-before-value early return (net-semantics regression pin), and
// EIP-7702 delegation to a precompile address.
//
// The wrapper bytecode is built by buildPrecompileWrapper (below); its
// disassembly was verified against the op-geth opcode table (see the Task 4
// report). It differs from the task brief's sketch in three ways, all required
// for the stated semantics to actually hold:
//
//  1. CALLDATACOPY: the brief's sketch read mem[0:CALLDATASIZE] as the inner
//     call's input, but calldata is never auto-copied to memory -- without
//     CALLDATACOPY the precompile would receive zero bytes (p256 verify would
//     fail, bn256 add would return the point-at-infinity). The builder copies
//     calldata to mem[0:cd] first, pushing length FIRST (CALLDATACOPY pops
//     memOffset, then dataOffset, then length -- geth opCallDataCopy).
//  2. CALL operand order: the sketch pushed (CALLDATASIZE PUSH0 PUSH0 PUSH1
//     0x20 PUSH20<addr> ... GAS CALL), which geth's opCall pops as
//     gas, addr=0, value=addr (calling address 0 with the precompile address
//     as value). The builder pushes retSize, retOffset, inSize, inOffset,
//     value, to, gas (the verified bottom-to-top order, same as the proven
//     beaconReaderCode precedent).
//  3. Revert-on-failure: the sketch's always-RETURN wrapper would yield tx
//     status 1 for the value-bearing failure cases, but the case table (and
//     the "revert 传播" acceptance) require status 0. The builder REVERTs
//     when the inner CALL returns 0, propagating the precompile failure.
//  4. RETURN operand order: the sketch's tail ... PUSH0 PUSH1 0x20 RETURN
//     pops offset=0x20, size=0 (geth opReturn pops offset first), returning
//     mem[32:0] = EMPTY -- so even the success path returned no data. The
//     builder pushes size then offset (PUSH1 0x20 PUSH0 RETURN) and drops the
//     pointless PUSH0 MLOAD.
package main

import (
	"math/big"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/common/hexutil"
	"github.com/ethereum/go-ethereum/core/types"
)

// Wrapper-contract and EIP-7702-authority addresses (continuation of the
// cases.go:51-56 c0de series; distinct range so peer Tasks 2/3 files cannot
// collide).
var (
	wrapAddr63 = common.HexToAddress("0xc0de000000000000000000000000000000000007")
	wrapAddrRd = common.HexToAddress("0xc0de000000000000000000000000000000000009")
	wrapAddrVr = common.HexToAddress("0xc0de00000000000000000000000000000000000a")
	wrapAddrVo = common.HexToAddress("0xc0de00000000000000000000000000000000000b")
)

// malformedBn256AddInput is a 128-byte bn256 add (0x06) input whose two G1
// points are both (x=1, y=3) -- NOT on alt_bn128 (y^2 = 9 != x^3+3 = 4). Both
// op-geth's newCurvePoint and FISCO's ecadd_execute reject it, so the
// precompile genuinely FAILS (EVMC_PRECOMPILE_FAILURE / "point is not on
// curve"), driving the wrapper's revert path.
//
// The task brief's value_revert row specified 0x100 (P256VERIFY) with a
// "malformed in-cap sig", but that precompile NEVER fails: op-geth
// p256Verify.Run returns (nil, nil) for both wrong-length and failed-verify
// input (contracts.go:1709-1731), and FISCO p256verify_execute returns
// {EVMC_SUCCESS, 0} for the same (precompiles.cpp:753-771) -- a malformed
// p256 input yields SUCCESS with empty output, not a revert. The only failure
// path of the 0x100 gas-override branch is the OOG check (OpHost.cpp:20-26,
// msg.gas < 3450), which would make the case a knife-edge gas test. 0x06 was
// chosen instead: a real precompile error, in-cap (generous gas), with the
// same observable net semantics the brief wants (value rolled back, wrapper
// status 0).
var malformedBn256AddInput = func() []byte {
	in := make([]byte, 128)
	in[31] = 1 // point1 x = 1
	in[63] = 3 // point1 y = 3
	in[95] = 1 // point2 x = 1
	in[127] = 3 // point2 y = 3
	return in
}()

// buildPrecompileWrapper assembles the canonical wrapper runtime bytecode:
//
//	CALLDATACOPY mem[0:cd] = calldata
//	CALL <precompile> with gas=GAS (EIP-150 caps to 63/64), value = 0 or
//	    CALLVALUE, in = mem[0:CALLDATASIZE] (the wrapper's own calldata),
//	    out = mem[0:32] (only the first 32 bytes of returndata are copied --
//	    the returndata-size quirk)
//	on CALL success: RETURN mem[0:32]
//	on CALL failure: REVERT (propagates the precompile failure -> tx status 0)
//
// Disassembly (verified against op-geth's opcode table; Task 4 report):
//
//	0000 CALLDATASIZE   length(CALLDATACOPY)
//	0001 PUSH0          dataOffset(CALLDATACOPY)
//	0002 PUSH0          memOffset(CALLDATACOPY)
//	0003 CALLDATACOPY   mem[0:cd] = calldata
//	0004 PUSH1 0x20     retSize
//	0006 PUSH0          retOffset
//	0007 CALLDATASIZE   inSize
//	0008 PUSH0          inOffset
//	0009 PUSH0/CALLVALUE value
//	000a PUSH20 <addr>  to
//	001f GAS            gas
//	0020 CALL           -> [success]
//	0021 PUSH2 0x28     dest
//	0024 JUMPI          if success -> 0x28
//	0025 PUSH0 PUSH0 REVERT   failure path
//	0028 JUMPDEST
//	0029 PUSH1 0x20     size(RETURN)
//	002b PUSH0          offset(RETURN)
//	002c RETURN         mem[0:32]
func buildPrecompileWrapper(addr []byte, withValue bool) []byte {
	head := hexutil.MustDecode("0x365f5f3760205f365f") // CALLDATASIZE PUSH0 PUSH0 CALLDATACOPY PUSH1 0x20 PUSH0 CALLDATASIZE PUSH0
	var value byte = 0x5f                              // PUSH0 (value = 0)
	if withValue {
		value = 0x34 // CALLVALUE
	}
	tail := hexutil.MustDecode("0x5af1610028575f5ffd5b60205ff3") // GAS CALL PUSH2 0x28 JUMPI PUSH0 PUSH0 REVERT JUMPDEST PUSH1 0x20 PUSH0 RETURN
	out := append(append(head, value), 0x73)                      // PUSH20 <addr>
	out = append(out, addr...)
	return append(out, tail...)
}

// wrapperFrame is precompileFrame + the wrapper contract pre-deployed at wrap
// (code + balance) and a single user tx appended after the attributes deposit.
// key 1 is the tx sender (precompileFrame funds it eth(100)); the wrapper's
// balance covers the value-bearing cases' 1-wei inner transfer.
func wrapperFrame(fork, name, desc string, wrap common.Address, code []byte, tx inputTx, blockGasLimit uint64) caseSpec {
	return caseSpec{
		name:  name,
		forks: []string{fork},
		build: func(f string) inputCase {
			c := caseFrame(f, name, desc, defaultFeeParams(), blockGasLimit)
			fund(&c, 1, eth(100))
			c.Pre[wrap] = types.Account{Code: code, Balance: eth(1)}
			c.Transactions = append(c.Transactions, tx)
			c.ExtraCandidates = append(c.ExtraCandidates, wrap)
			return c
		},
	}
}

func init() {
	caseSpecs = append(caseSpecs,

		// ist: 0x100 P256VERIFY, valid sig, no value. The wrapper CALLs with
		// gas=GAS (EIP-150 63/64 of remaining); the precompile's 3450 is
		// covered by the forwarded gas, the verify succeeds, and the wrapper
		// returns the 32-byte-1 the precompile wrote. gasUsed therefore
		// reflects: intrinsic + wrapper overhead + CALL cost + 3450 (the p256
		// consumption charged against the 63/64-forwarded gas).
		wrapperFrame("isthmus", "precompile_wrap_63of64",
			"wrapper contract CALLs P256VERIFY (0x100) with a valid sig, gas=GAS (EIP-150 63/64 forwarding); precompile's 3450 covered by forwarded gas; wrapper returns the 32-byte-1 success output",
			wrapAddr63, buildPrecompileWrapper(addrBytes(preP256Verify), false),
			precompileCallTx(wrapAddr63.Bytes(), validP256Sig(), 500_000, 0),
			10_000_000),

		// ist: 0x06 bn256 add, valid 2-point input, no value. The precompile
		// returns the 64-byte G1 sum; the wrapper's CALL copies only the first
		// 32 bytes (retSize 0x20) into mem[0:32] and returns them -- the
		// receipt output is the 32-byte x-coordinate, NOT the full 64-byte
		// point (returndata-size truncation quirk).
		//
		// NOTE: the brief's table lists 0x08 "valid pair" here, but 0x08
		// (bn256 pairing) returns 32 bytes -- a 32-of-32 copy is not an
		// observable truncation. The behavior column ("bn256 add 输出 64B")
		// and the returndata-size quirk require the 64-byte-output 0x06, which
		// takes two valid G1 points ("a pair" of points).
		wrapperFrame("isthmus", "precompile_wrap_returndata",
			"wrapper contract CALLs bn256 add (0x06) with two valid G1 points; 64B output truncated to the first 32B by the CALL's retSize (returndata-size quirk) -- receipt output is the x-coordinate only",
			wrapAddrRd, buildPrecompileWrapper(addrBytes(preBn256Add), false),
			precompileCallTx(wrapAddrRd.Bytes(), bn256AddInput, 500_000, 0),
			10_000_000),

		// jov: 0x06 bn256 add, malformed off-curve input, value 1 wei. The
		// precompile genuinely fails (invalid point -> EVMC_PRECOMPILE_FAILURE
		// on FISCO / "point is not on curve" on op-geth); the value transfer is
		// rolled back on both sides and the wrapper REVERTs, propagating the
		// failure -> tx status 0, no balance residue on 0x06. (0x100 was not
		// usable: P256VERIFY never errors on malformed input -- see
		// malformedBn256AddInput comment.)
		wrapperFrame("jovian", "precompile_wrap_value_revert",
			"wrapper contract CALLs bn256 add (0x06) with a malformed off-curve point and value 1 wei; precompile errors -> value rolled back, wrapper REVERT propagates the failure (tx status 0, no balance residue)",
			wrapAddrVr, buildPrecompileWrapper(addrBytes(preBn256Add), true),
			precompileCallTx(wrapAddrVr.Bytes(), malformedBn256AddInput, 500_000, 1),
			10_000_000),

		// jov: 0x08 bn256 pairing, 428 pairs (82176B > jovian cap 81984),
		// value 1 wei. OpHost's length-limit early return fires BEFORE value
		// application (cap 先于 value) and returns EVMC_FAILURE with 0 gas
		// (all forwarded gas consumed); op-geth transfers the value then
		// reverts it and zeroes the call gas. NET SEMANTICS are identical:
		// status 0, forwarded gas fully consumed, no value residue on 0x08.
		// tx gas 17M ensures the forwarded gas (63/64 of remaining after
		// intrinsic 1,007,112 + wrapper overhead + CALL cost) clears the
		// precompile's RequiredGas(428)=14,597,000 so the CAP CHECK (not an
		// OOG) is what fires -- the path this case pins.
		wrapperFrame("jovian", "precompile_wrap_value_overcap",
			"wrapper contract CALLs bn256 pairing (0x08) with an over-cap 428-pair input and value 1 wei; OpHost length-limit early return fires before value application -> status 0, all forwarded gas consumed, no value residue (net-semantics regression pin)",
			wrapAddrVo, buildPrecompileWrapper(addrBytes(preBn256Pairing), true),
			precompileCallTx(wrapAddrVo.Bytes(), repeatedBn256Pair(428), 17_000_000, 1),
			30_000_000),

		// ist: EIP-7702 setcode arm. The setcode tx (sponsor key 1) installs on
		// the authority (key 7) a delegation designator pointing at the 0x08
		// precompile; the tx then executes the delegated code -- empty (0x08
		// carries no state code). A second user tx calls the authority; FISCO's
		// state.cpp resolves the delegation (code_address=0x08, EVMC_DELEGATED)
		// and OpHost's EVMC_DELEGATED branch runs empty code instead of the
		// precompile. BOTH txs succeed (status 1) with empty output and no
		// storage writes -- the "双端执行空码" observable.
		caseSpec{"precompile_wrap_eip7702", []string{"isthmus"}, func(fork string) inputCase {
			c := caseFrame(fork, "precompile_wrap_eip7702",
				"EIP-7702 setcode tx installs a delegation designator to the 0x08 precompile on the authority; setcode tx AND a user call to the authority both execute EMPTY code (EVMC_DELEGATED branch) -- status 1, no storage, no precompile execution",
				defaultFeeParams(), 10_000_000)
			fund(&c, 1, eth(100))
			authority := addrOfKey(7)
			c.Pre[authority] = types.Account{Balance: eth(1)}
			sponsorKey := privKey(1)
			authKey := privKey(7)
			precompileAddr := common.BytesToAddress(addrBytes(preBn256Pairing))
			// Tx 1: setcode. Authorization signed by key 7 (the authority),
			// address = the 0x08 precompile -> the authority's code becomes
			// 0xef0100||0x08 (types.AddressToDelegation). The tx then executes
			// the delegated code (empty).
			c.Transactions = append(c.Transactions, inputTx{
				OpType:               "setcode",
				ChainID:              hd256(chainID),
				Nonce:                hd64(0),
				To:                   &authority,
				Value:                hd256(big.NewInt(0)),
				Gas:                  hd64(300_000),
				MaxFeePerGas:         hdu(2_000_000_000),
				MaxPriorityFeePerGas: hdu(100_000_000),
				SecretKey:            &sponsorKey,
				OpAuthorizations: []inputAuthorization{{
					ChainID:       hd256(chainID),
					Address:       precompileAddr,
					Nonce:         0,
					AuthSecretKey: authKey,
				}},
			})
			// Tx 2: user call to the authority (key 1, nonce 1). Delegation
			// resolves to the precompile -> empty code -> success, empty output.
			c.Transactions = append(c.Transactions, transferTx(1, 1, authority, big.NewInt(0), 100_000, nil))
			c.ExtraCandidates = append(c.ExtraCandidates, authority)
			return c
		}},
	)
}
