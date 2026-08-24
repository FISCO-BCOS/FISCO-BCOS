// Corpus definitions for the M-B3+M6 block-level vector matrix (plan Step 2
// table, 14 cases x fork column = 25 vectors; corpus augmentation spec rev.3
// adds 3 cases x both forks = 31 vectors; the defer-sweep batch adds
// empty_account_cleanup x both forks = 34 vectors). Phase 2 line A (Task 3)
// adds 7 vectors: 2 Ecotone + 2 Fjord formula-boundary + 3 upgrade-boundary
// activation cases = 41 vectors; line B (precompile matrix) adds 36 = 77
// vectors. `opt8n-ref --write-cases <dir>`
// re-emits the *.in.json files deterministically from these definitions, so
// the L1Block slot pre-seeding and the L1-attributes deposit calldata are
// built from ONE feeParams source and cannot drift apart -- and
// processBlockVector still re-asserts their consistency at startup.
package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math/big"
	"os"
	"path/filepath"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/common/hexutil"
	"github.com/ethereum/go-ethereum/common/math"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/crypto"
	"github.com/ethereum/go-ethereum/params"
	"github.com/holiman/uint256"
)

// l1BlockRuntimeCode is the real L1Block predeploy runtime bytecode
// (gen_l1block.py, tools/op-e2e, op-alignment branch) implementing
// setL1BlockValues{Ecotone,Isthmus,Jovian}: selector-dispatches, writes slots
// 1/3/7/8 from the L1-attributes calldata offsets that unpackOpFeeParams reads,
// reverts on calldatasize<4 or unknown selector. Injecting it into the vector
// genesis makes the C++ differential replayer execute the real predeploy path
// on EVERY deposit -- the same bytecode that carried the historical
// LT-direction / PUSH28-mask / JUMPDEST bugs is now under the op-geth-anchored
// gate, so a reintroduced bug diverges from op-geth's execution of the same
// code.
var l1BlockRuntimeCode = common.FromHex("0x6004361060255760003560e01c63098999be14602b5760003560e01c633db6be2b14602b575b60006000fd5b6000358060c01c63ffffffff1660601b60003560a01c63ffffffff1660401b176003555060243560015560443560075560a03560c01c63ffffffff1660401b60a03560801c67ffffffffffffffff161760b03560f01c61ffff1660601b1760085560006000f3")

// ---------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------

var (
	chainID = big.NewInt(8453) // 0x2105

	// Canonical L1-attributes depositor (L1 info sender). MUST be the real
	// op-stack constant 0xdead…dead0001 (op-geth eth/downloader/
	// receiptreference.go:28 systemAddress; op-node rollup/derive
	// L1InfoDepositerAddress) — the C++ replayer's processOpBlock hard-asserts
	// first-tx from == OP_DEPOSITOR (OpPredeploys.h, same constant). The
	// original dead×10 value was a corpus typo op-geth's own InsertChain
	// cannot catch (it never checks the depositor address).
	attributesDepositor = common.HexToAddress("0xDeaDDEaDDeAdDeAdDEAdDEaddeAddEAdDEAd0001")
	// Second depositor for user-deposit cases.
	userDepositor = common.HexToAddress("0x1111111111111111111111111111111111111111")

	recA = common.HexToAddress("0xb0b0000000000000000000000000000000000001")
	recB = common.HexToAddress("0xb0b0000000000000000000000000000000000002")
	recC = common.HexToAddress("0xb0b0000000000000000000000000000000000003")

	revertAddr   = common.HexToAddress("0xc0de000000000000000000000000000000000001")
	logsAddr     = common.HexToAddress("0xc0de000000000000000000000000000000000002")
	feeObsAddr   = common.HexToAddress("0xc0de000000000000000000000000000000000003")
	delegateAddr = common.HexToAddress("0xc0de000000000000000000000000000000000004")
	// access_list case: SLOAD target + a listed-but-never-called address.
	aclAddr       = common.HexToAddress("0xc0de000000000000000000000000000000000005")
	aclListedOnly = common.HexToAddress("0xdddddddddddddddddddddddddddddddddddddddd")
	// system_call_order_observable (B-7): user reader contract that CALLs the
	// EIP-4788 GET path (the BeaconRootsAddress predeploy) and SSTOREs the
	// returned root.
	readerAddr = common.HexToAddress("0xc0de000000000000000000000000000000000006")
	// gaslimit_observer / deposit_basefee_observer: 定向分叉观察者目标地址。
	// 审查 R1：用 0x...0007/0008 —— 0x...0004/0005 已被 delegateAddr/aclAddr 占用。
	gaslimitObsAddr = common.HexToAddress("0xc0de000000000000000000000000000000000007")
	basefeeObsAddr  = common.HexToAddress("0xc0de000000000000000000000000000000000008")
	// empty_account_cleanup case: pre-seeded exists-but-empty account
	// (balance 0, nonce 0, no code, no storage). NOT a precompile on either
	// side (exact-set membership; op-stack P256VERIFY sits at 0x…0100).
	emptyTouchAddr = common.HexToAddress("0x00000000000000000000000000000000000000e0")

	// PUSH0 PUSH0 REVERT
	revertCode = hexutil.MustDecode("0x5f5ffd")
	// SSTORE(0, 0x2a); LOG1 x3 (empty data, distinct topics); STOP
	// (assembled below in logsCode()).
	// SSTORE(1, CALLDATALOAD(0)); STOP
	messagePasserCode     = hexutil.MustDecode("0x5f3560015500")
	realMessagePasserCode = hexutil.MustDecode(
		"0x6080604052600436106100695760003560e01c806382e3702d1161004357806382e3702d1461012a578063c2b3e5ac1461016a578063ecc704281461017d5760" +
			"0080fd5b80633f827a5a1461009257806344df8e70146100bf57806354fd4d50146100d457600080fd5b3661008d5761008b33620186a0604051806020016040" +
			"528060008152506101e2565b005b600080fd5b34801561009e57600080fd5b506100a7600181565b60405161ffff90911681526020015b60405180910390f35b" +
			"3480156100cb57600080fd5b5061008b6103a6565b3480156100e057600080fd5b5061011d6040518060400160405280600581526020017f312e312e30000000" +
			"00000000000000000000000000000000000000000000000081525081565b6040516100b691906104d1565b34801561013657600080fd5b5061015a6101453660" +
			"046104eb565b60006020819052908152604090205460ff1681565b60405190151581526020016100b6565b61008b610178366004610533565b6101e2565b3480" +
			"1561018957600080fd5b506101d46001547dffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff167e0100000000000000000000000000" +
			"00000000000000000000000000000000001790565b6040519081526020016100b6565b60006102786040518060c0016040528061023c6001547dffffffffffff" +
			"ffffffffffffffffffffffffffffffffffffffffffffffff167e010000000000000000000000000000000000000000000000000000000000001790565b815233" +
			"602082015273ffffffffffffffffffffffffffffffffffffffff871660408201523460608201526080810186905260a0018490526103de565b60008181526020" +
			"8190526040902080547fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff00166001179055905073ffffffffffffffffffffffffff" +
			"ffffffffffffff8416336103136001547dffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff167e010000000000000000000000000000" +
			"000000000000000000000000000000001790565b7f02a52367d10742d8032712c1bb8e0144ff1ec5ffda1ed7d70bb05a27449550543487878760405161034894" +
			"93929190610637565b60405180910390a45050600180547dffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8082168301167fffff00" +
			"00000000000000000000000000000000000000000000000000000000009091161790555050565b476103b08161042b565b60405181907f7967de617a5ac1cc7e" +
			"ba2d6f37570a0135afa950d8bb77cdd35f0d0b4e85a16f90600090a250565b80516020808301516040808501516060860151608087015160a088015193516000" +
			"9761040e979096959101610667565b604051602081830303815290604052805190602001209050919050565b806040516104389061045a565b60405180910390" +
			"82f0905080158015610455573d6000803e3d6000fd5b505050565b6008806106bf83390190565b6000815180845260005b8181101561048c5760208185018101" +
			"5186830182015201610470565b8181111561049e576000602083870101525b50601f017fffffffffffffffffffffffffffffffffffffffffffffffffffffffff" +
			"ffffffe0169290920160200192915050565b6020815260006104e46020830184610466565b9392505050565b6000602082840312156104fd57600080fd5b5035" +
			"919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b600080600060608486031215" +
			"61054857600080fd5b833573ffffffffffffffffffffffffffffffffffffffff8116811461056c57600080fd5b925060208401359150604084013567ffffffff" +
			"ffffffff8082111561059057600080fd5b818601915086601f8301126105a457600080fd5b8135818111156105b6576105b6610504565b604051601f82017fff" +
			"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe0908116603f011681019083821181831017156105fc576105fc610504565b816040" +
			"5282815289602084870101111561061557600080fd5b8260208601602083013760006020848301015280955050505050509250925092565b8481528360208201" +
			"526080604082015260006106566080830185610466565b905082606083015295945050505050565b868152600073ffffffffffffffffffffffffffffffffffff" +
			"ffff808816602084015280871660408401525084606083015283608083015260c060a08301526106b260c0830184610466565b9897505050505050505056fe60" +
			"8060405230fffea164736f6c634300080f000a")

	// SSTORE(1, 0x2a); STOP -- EIP-7702 delegate target
	delegateCode = hexutil.MustDecode("0x602a60015500")
	// SSTORE(0, GASPRICE); SSTORE(1, BASEFEE); SSTORE(2, SELFBALANCE); STOP
	feeObserverCode = hexutil.MustDecode("0x3a5f55486001554760025500")
	// spec §6：gaslimit: GASLIMIT(0x45) PUSH1 0 SSTORE STOP —— 存 gaslimit()。
	// basefee: BASEFEE(0x48) PUSH1 0 SSTORE STOP —— 存 basefee()。
	gaslimitObserverCode = hexutil.MustDecode("0x4560005500")
	basefeeObserverCode  = hexutil.MustDecode("0x4860005500")
	// contract_create initcode: init phase SSTORE(0,1), then the classic
	// PUSH+MSTORE+RETURN deploy of the 6-byte runtime 0x600160005500
	// (= SSTORE(0,1); STOP; never called in-block):
	//   6001 6000 55            SSTORE(0, 1)
	//   65 600160005500         PUSH6 <runtime>
	//   6000 52                 MSTORE(0, runtime)   (right-aligned: bytes 26..31)
	//   6006 601a f3            RETURN(0x1a, 6)
	createInitCode = hexutil.MustDecode("0x6001600055656001600055006000526006601af3")
	// access_list target: SLOAD(0); SLOAD(2); STOP. slot0 = listed+accessed,
	// slot2 = accessed-NOT-listed (the cold-price discriminator).
	aclCode = hexutil.MustDecode("0x60005460025400")

	// EIP-4788 beacon roots contract, deployed runtime bytecode.
	// Provenance: execution-specs checkout
	// /Users/octopus/octo/code/blockchain-impl/execution-specs
	// @ c3462e030f7e2cebe93688d15d2423dcf16fc8cc,
	// packages/testing/src/execution_testing/forks/forks/eips/cancun/eip_4788.py
	// (EEST fork pre-allocation; NOT hand-typed).
	beaconRootsCode = hexutil.MustDecode("0x3373fffffffffffffffffffffffffffffffffffffffe14604d57602036146024575f5ffd5b5f35801560495762001fff810690815414603c575f5ffd5b62001fff01545f5260205ff35b5f5ffd5b62001fff42064281555f359062001fff015500")
	// EIP-2935 history storage contract, deployed runtime bytecode.
	// Provenance: same checkout,
	// packages/testing/src/execution_testing/forks/forks/eips/prague/contracts/history_contract.bin
	// (xxd dump; NOT hand-typed).
	historyStorageCode = hexutil.MustDecode("0x3373fffffffffffffffffffffffffffffffffffffffe14604657602036036042575f35600143038111604257611fff81430311604257611fff9006545f5260205ff35b5f5ffd5b5f35611fff60014303065500")

	// EIP-4788 order-observable reader (B-7), hand-assembled; disassembly in
	// Task 1 report. BUILT from params.BeaconRootsAddress so the CALL target
	// can never drift from the real EIP-4788 predeploy (0xfff...ffe is the
	// SystemAddress, which carries no code). Flow:
	//   TIMESTAMP; PUSH0; MSTORE             mem[0:32] = block.timestamp
	//   PUSH2 0x1fff; TIMESTAMP; MOD          idx = t % 8191 (own storage slot)
	//   PUSH1 32; PUSH0; PUSH1 32; PUSH0; PUSH0
	//   PUSH20 <BeaconRootsAddress>; GAS; CALL   GET (calldata=timestamp; out buf mem[0:32])
	//   POP; PUSH1 32; PUSH0; PUSH0; RETURNDATACOPY  no-op on success (root is already
	//                                        in mem[0:32] via the CALL out-buffer); the
	//                                        "empty returndata halts" path is what makes
	//                                        the wrong-order reader tx fail
	//   PUSH0; MLOAD; SWAP1; SSTORE; STOP     storage[idx] = returned root
	// The 4788 GET path (beaconRootsCode) only returns a value once its
	// ts==sload(ts) ring check passes, which happens ONLY AFTER the block's
	// 4788 system call overwrote the stale pre-seeded ring slot -- so a
	// replayer that runs user txs before the system call makes the reader's
	// CALL revert (empty returndata -> RETURNDATACOPY halts) and the reader tx
	// diverges (status 0, no storage write).
	beaconReaderCode = func() []byte {
		head := hexutil.MustDecode("0x425f52611fff420660205f60205f5f73") // up to the PUSH20
		tail := hexutil.MustDecode("0x5af15060205f5f3e5f51905500")       // GAS CALL POP RDC PUSH0 MLOAD SWAP1 SSTORE STOP
		out := append(head, params.BeaconRootsAddress.Bytes()...)
		return append(out, tail...)
	}()

	// -----------------------------------------------------------------
	// Phase 2 line B (Task 1): precompile vector INPUTS. Each is genuinely
	// valid so the precompile succeeds where the plan says success (verified
	// end-to-end by --probe-receipt-fields during Task 1; construction detail
	// in the Task 1 report).
	// -----------------------------------------------------------------

	// validEcrecoverInput is a REAL secp256k1 signature recovering to key 1
	// (0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf). hash = sha256("fisco
	// line-b ecrecover"), signed with privKey(1); recovery verified against
	// crypto.Ecrecover during generation. Layout follows op-geth core/vm
	// ecrecover.Run: hash(32) || v-slot(32, last byte = recovery-id + 27) ||
	// r(32) || s(32).
	validEcrecoverInput = hexutil.MustDecode("0x" +
		"34fd596c5986a266fe0e5c5f2a160701bb8877079ecc6f617c81bfddcdbbbe5b" + // hash
		"000000000000000000000000000000000000000000000000000000000000001b" + // v-slot (recid 0 -> 0x1b)
		"a048fc6ef02730c5eff7fedfdaa9ed5f27725d2ae4edea299ec783192c6adf9e" + // r
		"62d797bf5a0d6cb3cd9f2b5caa77ef735a2cffe4f1907d8c880c7157e0f4d074") // s

	// validBlake2fInput is EIP-152 test vector 5 (the BLAKE2b F-compression
	// "abc" case): rounds=12, h = BLAKE2b IV ^ 0x01010040, m = "abc", final=1.
	// Provenance: op-geth core/vm/testdata/precompiles/blake2F.json vector 5
	// (canonical; Expected = ba80a53f…409923).
	validBlake2fInput = hexutil.MustDecode("0x0000000c48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b61626300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000300000000000000000000000000000001")

	// expmodZeroHeader: 96B modexp header with baseLen=expLen=modLen=0 (empty
	// base/exp/mod). op-geth bigModExp.Run returns []byte{} for baseLen==0 &&
	// modLen==0 (EIP-198) -- SUCCESS (status 1) with an empty output, NOT "1"
	// as the Task 1 brief stated (corrected; the receipt is unaffected).
	expmodZeroHeader = make([]byte, 96)

	// bn256AddInput: two valid G1 points (128B, EIP-196).
	bn256AddInput = repeatedPair(validBn256G1(), 2)
	// bn256MulInput: valid G1 point (64B) || scalar 0x02 (32B) = 96B.
	bn256MulInput = append(validBn256G1(), common.BigToHash(big.NewInt(2)).Bytes()...)

	// deadbeefInput: shared 4-byte input for sha256/ripemd160/identity.
	deadbeefInput = hexutil.MustDecode("0xdeadbeef")
)

// ---------------------------------------------------------------------
// Precompile addresses (EIP-196/197/198/152/2537/4844/7212 + OP extension)
// ---------------------------------------------------------------------

const (
	preEcRecover    = 0x01
	preSha256       = 0x02
	preRipemd160    = 0x03
	preIdentity     = 0x04
	preExpMod       = 0x05
	preBn256Add     = 0x06
	preBn256Mul     = 0x07
	preBn256Pairing = 0x08
	preBlake2f      = 0x09
	prePointEval    = 0x0a // EIP-4844 KZG point evaluation
	preBlsG1Add     = 0x0b
	preBlsG1MSM     = 0x0c
	preBlsG2Add     = 0x0d
	preBlsG2MSM     = 0x0e
	preBlsPairing   = 0x0f
	preBlsMapG1     = 0x10
	preBlsMapG2     = 0x11
	preP256Verify   = 0x100 // RIP-7212; 20-byte address 0x…0100 (two low bytes)
)

// addrBytes(n) returns the 20-byte precompile address. The single-byte
// precompiles (0x01..0x11) sit at 0x…00nn; P256VERIFY (0x100) needs the
// second-lowest byte set (0x…0100), so values above 0xFF write into b[18].
func addrBytes(n uint) []byte {
	b := make([]byte, 20)
	if n > 0xFF {
		b[18] = byte(n >> 8)
	}
	b[19] = byte(n)
	return b
}

// senderHex returns the checksummed hex form of the EOA for private key i
// (key 1 is the standard precompile-call sender; precompileFrame funds it).
func senderHex(i byte) string {
	return addrOfKey(i).Hex()
}

func logsCode() []byte {
	code := hexutil.MustDecode("0x602a5f55") // SSTORE(0, 0x2a)
	for i := 1; i <= 3; i++ {
		topic := crypto.Keccak256([]byte(fmt.Sprintf("opt8n-ref topic %d", i)))
		code = append(code, 0x7f)       // PUSH32
		code = append(code, topic...)   //   topic
		code = append(code, 0x5f, 0x5f) // PUSH0 PUSH0 (size, offset)
		code = append(code, 0xa1)       // LOG1
	}
	return append(code, 0x00) // STOP
}

// ---------------------------------------------------------------------
// Small builders
// ---------------------------------------------------------------------

func hd64(v uint64) *math.HexOrDecimal64 { h := math.HexOrDecimal64(v); return &h }
func hd256(v *big.Int) *math.HexOrDecimal256 {
	return (*math.HexOrDecimal256)(new(big.Int).Set(v))
}
func hdu(v uint64) *math.HexOrDecimal256 { return hd256(new(big.Int).SetUint64(v)) }

func eth(n int64) *big.Int { // n * 10^18
	return new(big.Int).Mul(big.NewInt(n), big.NewInt(1_000_000_000_000_000_000))
}
func milliEth(n int64) *big.Int { // n * 10^15
	return new(big.Int).Mul(big.NewInt(n), big.NewInt(1_000_000_000_000_000))
}

func privKey(i byte) hexutil.Bytes {
	k := make([]byte, 32)
	k[31] = i
	return k
}

func addrOfKey(i byte) common.Address {
	key, err := crypto.ToECDSA(privKey(i))
	if err != nil {
		panic(err)
	}
	return crypto.PubkeyToAddress(key.PublicKey)
}

func junkData(label string, n int) hexutil.Bytes {
	out := make([]byte, 0, n+32)
	cur := crypto.Keccak256([]byte("opt8n-ref junk " + label))
	for len(out) < n {
		out = append(out, cur...)
		cur = crypto.Keccak256(cur)
	}
	return out[:n]
}

func sourceHash(label string) common.Hash {
	return crypto.Keccak256Hash([]byte("opt8n-ref source " + label))
}

// l1AttributesLayout is the L1-attributes deposit layout for an OP-Stack fork
// (the per-fork byte form of the L1InfoDeposit calldata, mirroring op-geth
// core/types/rollup_cost.go extractL1GasParamsPostEcotone/PostIsthmus and
// ExtractDAFootprintGasScalar). Ecotone/Fjord/Granite/Holocene all use the
// 164-byte Ecotone layout; only Isthmus adds the operator-fee segment and
// Jovian the DA-footprint scalar.
type l1AttributesLayout int

const (
	layoutEcotone l1AttributesLayout = iota // 164B, selector 0x440a5e20, no operator-fee/DA segment
	layoutIsthmus                           // 176B, selector 0x098999be, + operatorFeeScalar/Constant [164:176]
	layoutJovian                            // 178B, selector 0x3db6be2b, + daFootprintGasScalar [176:178]
)

// forkLayout maps a hardfork name to its L1-attributes layout. The four
// Ecotone-family forks (ecotone/fjord/granite/holocene) are byte-identical for
// L1 attributes -- op-geth only branches on the Ecotone selector, and
// extractL1GasParamsPostEcotone hard-rejects any non-164-byte payload.
func forkLayout(fork string) l1AttributesLayout {
	switch fork {
	case "ecotone", "fjord", "granite", "holocene":
		return layoutEcotone
	case "isthmus":
		return layoutIsthmus
	case "jovian":
		return layoutJovian
	default:
		panic(fmt.Sprintf("unknown hardfork %q in forkLayout", fork))
	}
}

// feeParams is the single source for BOTH the L1Block storage pre-seeding and
// the L1-attributes deposit calldata (iron rule: field-for-field identical).
type feeParams struct {
	l1BaseFee         *big.Int
	blobBaseFee       *big.Int
	baseFeeScalar     uint32
	blobBaseFeeScalar uint32
	opFeeScalar       uint32
	opFeeConstant     uint64
	daScalar          uint16
	isthmusLayout     bool // force 176B Isthmus-selector calldata under a Jovian config (activation form)
}

func defaultFeeParams() feeParams {
	return feeParams{
		l1BaseFee:         big.NewInt(30_000_000_000), // 30 gwei
		blobBaseFee:       big.NewInt(1_000_000),
		baseFeeScalar:     1368,
		blobBaseFeeScalar: 810949,
	}
}

func (fp feeParams) attributesData(fork string) hexutil.Bytes {
	layout := forkLayout(fork)
	if layout == layoutJovian && fp.isthmusLayout {
		layout = layoutIsthmus // Jovian activation form: Isthmus-length/selector calldata (case 12)
	}
	var size int
	var selector []byte
	switch layout {
	case layoutEcotone:
		size, selector = 164, types.EcotoneL1AttributesSelector // extractL1GasParamsPostEcotone: hard 164
	case layoutIsthmus:
		size, selector = types.IsthmusL1AttributesLen, types.IsthmusL1AttributesSelector
	case layoutJovian:
		size, selector = types.JovianL1AttributesLen, types.JovianL1AttributesSelector
	}
	buf := make([]byte, size)
	copy(buf[0:4], selector)
	binary.BigEndian.PutUint32(buf[4:8], fp.baseFeeScalar)
	binary.BigEndian.PutUint32(buf[8:12], fp.blobBaseFeeScalar)
	// [12:20] sequenceNumber, [20:28] l1 timestamp, [28:36] l1 number: zero
	// (not read by any cost function; not slot-mirrored -- see README).
	fp.l1BaseFee.FillBytes(buf[36:68])
	fp.blobBaseFee.FillBytes(buf[68:100])
	// [100:132] l1 block hash, [132:164] batcher hash: zero.
	if layout != layoutEcotone {
		// Operator-fee segment exists ONLY in Isthmus+ layouts (the Ecotone
		// 164B form has no [164:176] -- writing it would make data 176B and
		// extractL1GasParamsPostEcotone would reject it).
		binary.BigEndian.PutUint32(buf[164:168], fp.opFeeScalar)
		binary.BigEndian.PutUint64(buf[168:176], fp.opFeeConstant)
	}
	if layout == layoutJovian {
		binary.BigEndian.PutUint16(buf[176:178], fp.daScalar)
	}
	return buf
}

func (fp feeParams) l1BlockStorage(fork string) map[common.Hash]common.Hash {
	st := map[common.Hash]common.Hash{}
	st[types.L1BaseFeeSlot] = common.BigToHash(fp.l1BaseFee)
	st[types.L1BlobBaseFeeSlot] = common.BigToHash(fp.blobBaseFee)
	var slot3 common.Hash
	binary.BigEndian.PutUint32(slot3[16:20], fp.baseFeeScalar)
	binary.BigEndian.PutUint32(slot3[20:24], fp.blobBaseFeeScalar)
	st[types.L1FeeScalarsSlot] = slot3
	var slot8 common.Hash
	switch forkLayout(fork) {
	case layoutJovian:
		if !fp.isthmusLayout {
			binary.BigEndian.PutUint16(slot8[18:20], fp.daScalar)
		}
		binary.BigEndian.PutUint32(slot8[20:24], fp.opFeeScalar)
		binary.BigEndian.PutUint64(slot8[24:32], fp.opFeeConstant)
	case layoutIsthmus:
		binary.BigEndian.PutUint32(slot8[20:24], fp.opFeeScalar)
		binary.BigEndian.PutUint64(slot8[24:32], fp.opFeeConstant)
	case layoutEcotone:
		// No operator-fee / DA segment in the Ecotone 164B layout: slot 8
		// (OperatorFeeParams) stays unseeded -- it is an Isthmus+ concept.
	}
	if slot8 != (common.Hash{}) {
		st[types.OperatorFeeParamsSlot] = slot8
	}
	return st
}

func (fp feeParams) attributesTx(label string, fork string) inputTx {
	to := l1BlockAddr
	return inputTx{
		OpType: "deposit",
		OpDeposit: &inputDeposit{
			From:       attributesDepositor,
			To:         &to,
			Gas:        math.HexOrDecimal64(1_000_000),
			IsSystemTx: false,
			SourceHash: sourceHash("attributes " + label),
		},
		Data: fp.attributesData(fork),
	}
}

func abiEncodeWithdrawal(versionedNonce *big.Int, sender, target common.Address, value, gasLimit uint64, data []byte) []byte {
	word := func() []byte { return make([]byte, 32) }
	putUint64 := func(w []byte, v uint64) {
		for i := range w {
			w[i] = 0
		}
		binary.BigEndian.PutUint64(w[24:], v)
	}
	head := make([]byte, 6*32)
	versionedNonce.FillBytes(head[0:32])
	copy(head[44:64], sender[:])
	copy(head[76:96], target[:])
	putUint64(head[96:128], value)
	putUint64(head[128:160], gasLimit)
	putUint64(head[160:192], 6*32) // data offset
	out := append([]byte{}, head...)
	lenWord := word()
	putUint64(lenWord, uint64(len(data)))
	out = append(out, lenWord...)
	out = append(out, data...)
	out = append(out, make([]byte, 32-len(data))...)
	return out
}

func transferTx(key byte, nonce uint64, to common.Address, value *big.Int, gas uint64, data hexutil.Bytes) inputTx {
	k := privKey(key)
	toCopy := to
	return inputTx{
		OpType:               "eip1559",
		ChainID:              hd256(chainID),
		Nonce:                hd64(nonce),
		To:                   &toCopy,
		Value:                hd256(value),
		Gas:                  hd64(gas),
		MaxFeePerGas:         hdu(2_000_000_000), // 2 gwei
		MaxPriorityFeePerGas: hdu(100_000_000),   // 0.1 gwei
		Data:                 data,
		SecretKey:            &k,
	}
}

// legacyTransferTx builds a type-0 legacy tx with an EIP-155 protected
// signature (B-scope base). gasPrice is the single per-gas price; 2 gwei is
// above the 1 gwei base fee of every caseFrame so the transfer is payable.
func legacyTransferTx(key byte, nonce uint64, to common.Address, value *big.Int, gas uint64, data hexutil.Bytes) inputTx {
	k := privKey(key)
	toCopy := to
	return inputTx{
		OpType:    "legacy",
		ChainID:   hd256(chainID),
		Nonce:     hd64(nonce),
		To:        &toCopy,
		Value:     hd256(value),
		Gas:       hd64(gas),
		GasPrice:  hdu(2_000_000_000), // 2 gwei
		Data:      data,
		SecretKey: &k,
	}
}

// caseFrame assembles the shared skeleton: genesis knobs, coinbase, beacon
// root, and the baseline pre (L1Block slots seeded from fp; the
// L2ToL1MessagePasser always on record per plan Step 2).
func caseFrame(fork, name, desc string, fp feeParams, gasLimit uint64) inputCase {
	jovianCfg := fork == "jovian"
	knobs := genesisKnobs{
		Timestamp:          math.HexOrDecimal64(1000),
		GasLimit:           math.HexOrDecimal64(gasLimit),
		BaseFee:            hdu(1_000_000_000), // 1 gwei
		EIP1559Denominator: math.HexOrDecimal64(50),
		EIP1559Elasticity:  math.HexOrDecimal64(6),
	}
	if jovianCfg {
		knobs.MinBaseFee = hd64(0)
	}
	// L1Block carries the REAL predeploy runtime code (l1BlockRuntimeCode), so
	// the attributes deposit actually executes setL1BlockValues* and writes
	// slots 1/3/7/8 from the calldata -- mirroring the real chain. Because the
	// calldata and the pre-seeded slots are built from ONE feeParams source
	// (iron rule below), the code writes exactly the seeded values; the C++
	// replayer then sees the genuine deposit→predeploy execution path against
	// op-geth's execution of the same bytecode (the LT-direction / PUSH28-mask
	// / JUMPDEST regressions of the historical handwritten bytecode are now
	// under the op-geth-anchored differential gate). Nonce 1 + code keeps the
	// account non-empty under EIP-158.
	pre := types.GenesisAlloc{
		l1BlockAddr:       {Balance: big.NewInt(0), Nonce: 1, Code: l1BlockRuntimeCode, Storage: fp.l1BlockStorage(fork)},
		messagePasserAddr: {Balance: big.NewInt(0), Nonce: 1},
	}
	return inputCase{
		Info:                  caseInfo{Hardfork: fork, Description: desc},
		Genesis:               knobs,
		Coinbase:              sequencerVault,
		ParentBeaconBlockRoot: common.HexToHash("0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"),
		Pre:                   pre,
		Transactions: []inputTx{
			fp.attributesTx(fork+"_"+name, fork),
		},
	}
}

// opForkOrder is the strict OP-Stack fork sequence. upgradeFrame activates
// EVERY fork strictly after baseFork and up to (and including) activationFork
// at activationT: real OP chains cannot skip forks -- a fjord->isthmus upgrade
// must also fire granite+holocene, otherwise the block's extraData stays empty
// (pre-Holocene) instead of the mandatory 9-byte Holocene form. The base fork
// itself and all forks before it stay at 0 (already active at genesis).
var opForkOrder = []string{"ecotone", "fjord", "granite", "holocene", "isthmus", "jovian"}

// upgradeFrame assembles the upgrade-boundary skeleton (spec A2, Task 3):
// genesis in baseFork, single block (timestamp = genesis+10 = 1010) crossing
// into activationFork at activationT = 1005 -- the 10-second coupling
// (1000 < 1005 <= 1010 holds, so the single block is the FIRST block of the
// new fork). The L1-attributes deposit format switches at the fork boundary
// (op-geth extractL1GasParams branches on the selector), so the L1Block slot
// seeding and the deposit calldata are REBUILT under the activation fork's
// layout -- still from the same feeParams (iron rule: slot <-> calldata).
func upgradeFrame(baseFork, name, desc string, fp feeParams, gasLimit uint64, activationFork string, activationT uint64) inputCase {
	c := caseFrame(baseFork, name, desc, fp, gasLimit)
	c.Pre[l1BlockAddr] = types.Account{Balance: big.NewInt(0), Nonce: 1, Code: l1BlockRuntimeCode, Storage: fp.l1BlockStorage(activationFork)}
	c.Transactions[0] = fp.attributesTx(baseFork+"_"+name, activationFork)
	c.Info.Activations = map[string]uint64{}
	started := false
	for _, f := range opForkOrder {
		if !started {
			if f == baseFork {
				started = true
			}
			continue
		}
		c.Info.Activations[f] = activationT
		if f == activationFork {
			break
		}
	}
	if activationFork == "jovian" {
		// EncodeOptimismExtraData panics on nil minBaseFee for Jovian BLOCKS;
		// processBlockVector gates minBaseFee on blockTime (Task 3 handoff A),
		// so a genesis that is pre-Jovian but crosses JovianTime must still
		// carry genesis.minBaseFee.
		c.Genesis.MinBaseFee = hd64(0)
	}
	// _info.hardfork carries the BLOCK-TIME fork (the upgrade TARGET), matching
	// its established meaning across every other vector -- "the fork the block
	// executes under" -- so the C++ differential-gate replayer (which selects
	// its chain config from _info.hardfork ONLY and never reads activations)
	// replays the block under the target fork instead of the genesis base fork.
	// The base fork lives only in the vector FILE name (vectorName) and in the
	// activations schedule. buildConfigForCase still derives an IDENTICAL chain
	// config from (hardfork = target, activations): the target-as-base keeps
	// every fork through the target at 0 and the activations map re-timestamps
	// exactly the forks that fire at activationT, so op-geth generation output
	// is byte-for-byte unchanged -- this is a metadata-only edit.
	c.Info.Hardfork = activationFork
	return c
}

func fund(c *inputCase, key byte, amount *big.Int) {
	c.Pre[addrOfKey(key)] = types.Account{Balance: amount}
}

// precompileCallTx builds a direct EIP-1559 call to a precompile address
// (To = the 20-byte precompile address, Data = the precompile input). Field
// shape mirrors transferTx (cases.go) / buildTx's eip1559 arm (main.go):
// sender is key 1 (SecretKey), nonce 0 -- precompileFrame funds key 1, and a
// frame holding several precompile txs must raise nonces at its call site.
// gas is the full tx gas limit (line-B §4.1: cap-over-limit vectors need
// gas >= RequiredGas(cap-sized input) so the call reaches the precompile's own
// cap check instead of OOG-ing first); value defaults to 0.
func precompileCallTx(addr []byte, data []byte, gas uint64, value uint64) inputTx {
	k := privKey(1)
	to := common.BytesToAddress(addr)
	return inputTx{
		OpType:               "eip1559",
		ChainID:              hd256(chainID),
		Nonce:                hd64(0),
		To:                   &to,
		Value:                hd256(new(big.Int).SetUint64(value)),
		Gas:                  hd64(gas),
		MaxFeePerGas:         hdu(2_000_000_000), // 2 gwei
		MaxPriorityFeePerGas: hdu(100_000_000),   // 0.1 gwei
		Data:                 data,
		SecretKey:            &k,
	}
}

// precompileFrame assembles a single-block precompile case on top of the
// line-A caseFrame skeleton (genesis knobs, coinbase, beacon root, L1Block
// slot seeding, L1-attributes deposit tx0). txs are appended after the
// attributes deposit; sender key 1 is funded eth(100) -- plenty for a ~20M-gas
// over-limit call at 2 gwei plus its calldata/L1 fee.
//
// blockGasLimit is threaded straight into caseFrame's gasLimit -> genesisKnobs
// GasLimit -> inputCase.Genesis.GasLimit, which processBlockVector
// (main.go:652) reads verbatim as core.Genesis.GasLimit; the single generated
// block inherits the genesis gas limit, so a raised value (30M, big_block
// precedent) is what lets cap-over-limit vectors clear the block-level OOG and
// actually reach the precompile cap path. Default is 10_000_000 (line-A norm).
//
// It returns a caseSpec for Tasks 1-4 to drop into the caseSpecs table (Task 0
// itself adds no vectors, keeping regen.sh's 41-case count stable; the
// --probe-precompile dev probe exercises this constructor end-to-end).
func precompileFrame(fork, name, desc string, txs []inputTx, blockGasLimit uint64) caseSpec {
	return caseSpec{
		name:  name,
		forks: []string{fork},
		build: func(f string) inputCase {
			c := caseFrame(f, name, desc, defaultFeeParams(), blockGasLimit)
			fund(&c, 1, eth(100))
			c.Transactions = append(c.Transactions, txs...)
			return c
		},
	}
}

// basePrecompileCase is the shared shape for the base-7 + bn256 add/mul normal
// vectors (line B Task 1 steps 1-2): fork=isthmus, a single direct EIP-1559
// call to precompile addr with a VALID input. tx gas 500_000 covers the
// intrinsic gas (~21.5k) PLUS the precompile's RequiredGas (see the Task 0
// probe note: a 150-gas tx would OOG at the intrinsic check and never reach
// the precompile); block gas 10M (precompileFrame default).
func basePrecompileCase(name, desc string, addr uint, input []byte) caseSpec {
	return precompileFrame("isthmus", name, desc,
		[]inputTx{precompileCallTx(addrBytes(addr), input, 500_000, 0)},
		10_000_000)
}

// bn256 pairing over-cap tx gas (line B §4.1 / Task 1 brief constraint
// "tx gas >= max(intrinsic, floor, RequiredGas) + margin"). The tx gas limit
// is charged intrinsic FIRST (state_transition.go:563), so it must clear
// intrinsic + RequiredGas or the precompile OOGs before its cap check.
// RequiredGas = 45000 + 34000×pairs (Istanbul rate; Granite/Jovian defer to
// it). Measured (Task 1 report, reproducible):
//
//	587 pairs (granite/fjord): intrinsic 1,373,448 + RequiredGas 20,003,000
//	                           = 21,376,448 -> 21,400,000
//	428 pairs (jovian):        intrinsic 1,007,112 + RequiredGas 14,597,000
//	                           = 15,604,112 -> 15,700,000
//
// The EIP-7623 floor (3.40M / 2.49M) does not bind. EIP-7825's 16.7M MaxTxGas
// cap does NOT apply: OsakaTime is nil in every OP vector config
// (buildChainConfig), so rules.IsOsaka is false.
const (
	bn256PairOvercapGas587 = 21_400_000
	bn256PairOvercapGas428 = 15_700_000
)

// ---------------------------------------------------------------------
// The 18 cases (14 from the M-B3+M6 plan table + 3 corpus-augmentation
// cases, spec rev.3, + 1 defer-sweep case)
// ---------------------------------------------------------------------

type caseSpec struct {
	name  string
	forks []string
	build func(fork string) inputCase
}

// bScopeSpecs names the B-scope bases (Task 3 F1): buildable for
// corrupt/invalid-tx bases but NOT emitted by --write-cases / emitCases (the
// T8n replayer has no consumer arm for them yet). legacy_transfer WAS the sole
// bScope spec; the replayer legacy arm landed in Task 6 (OpT8nReplayTest.cpp),
// so the set is now EMPTY and legacy_transfer is emitted normally. Kept as a
// set (not a struct field) so the shared table's positional literals stay
// untouched; re-populate it if a future base outpaces its consumer arm.
var bScopeSpecs = map[string]bool{}

var bothForks = []string{"isthmus", "jovian"}

var caseSpecs = []caseSpec{
	{"deposit_only", bothForks, func(fork string) inputCase {
		return caseFrame(fork, "deposit_only",
			"L1 attributes deposit only; header commitments over an otherwise empty block",
			defaultFeeParams(), 10_000_000)
	}},

	{"transfer_basic", bothForks, func(fork string) inputCase {
		c := caseFrame(fork, "transfer_basic",
			"attributes + one EIP-1559 value transfer",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, recA, eth(1), 21_000, nil))
		return c
	}},

	{"legacy_transfer", bothForks, func(fork string) inputCase {
		// B-scope base: a type-0 legacy tx with an EIP-155 protected signature
		// (V = chainID*2+35/36). Serves as the base for corrupt/invalid-tx
		// vectors and as a B-scope valid vector. Replayer legacy arm landed in
		// Task 6 (OpT8nReplayTest.cpp legacy 臂), so it is emitted by
		// --write-cases (bScopeSpecs is empty).
		c := caseFrame(fork, "legacy_transfer",
			"attributes + one type-0 EIP-155 protected legacy value transfer",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, legacyTransferTx(1, 0, recA, eth(1), 21_000, nil))
		return c
	}},

	{"transfer_multi", bothForks, func(fork string) inputCase {
		c := caseFrame(fork, "transfer_multi",
			"attributes + five transfers from three senders with interleaved nonces",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		fund(&c, 2, eth(100))
		fund(&c, 3, eth(100))
		c.Transactions = append(c.Transactions,
			transferTx(1, 0, recA, eth(1), 21_000, nil),
			transferTx(2, 0, recB, eth(2), 21_000, nil),
			transferTx(1, 1, recC, milliEth(500), 21_000, nil),
			transferTx(3, 0, recA, milliEth(250), 21_000, nil),
			transferTx(2, 1, recB, milliEth(750), 21_000, nil),
		)
		return c
	}},

	{"deposit_mint", bothForks, func(fork string) inputCase {
		c := caseFrame(fork, "deposit_mint",
			"attributes + user deposit with mint and value to an EOA",
			defaultFeeParams(), 10_000_000)
		to := recA
		c.Transactions = append(c.Transactions, inputTx{
			OpType: "deposit",
			OpDeposit: &inputDeposit{
				From:       userDepositor,
				To:         &to,
				Mint:       hd256(eth(2)),
				Value:      hd256(eth(1)),
				Gas:        math.HexOrDecimal64(50_000),
				SourceHash: sourceHash("deposit_mint " + fork),
			},
		})
		return c
	}},

	{"deposit_failed", bothForks, func(fork string) inputCase {
		// EVM-revert deposit: status 0 with ACTUAL gasUsed (gasUsed==gasLimit
		// happens only for consensus-failing deposits, which cannot appear in
		// a valid block -- see README "Known boundaries"). Mint is still
		// credited to the sender; the value transfer is rolled back.
		c := caseFrame(fork, "deposit_failed",
			"attributes + user deposit that reverts in the EVM (status 0, actual gasUsed, mint still credited)",
			defaultFeeParams(), 10_000_000)
		c.Pre[revertAddr] = types.Account{Balance: big.NewInt(0), Code: revertCode}
		to := revertAddr
		c.Transactions = append(c.Transactions, inputTx{
			OpType: "deposit",
			OpDeposit: &inputDeposit{
				From:       userDepositor,
				To:         &to,
				Mint:       hd256(eth(1)),
				Value:      hd256(milliEth(500)),
				Gas:        math.HexOrDecimal64(100_000),
				SourceHash: sourceHash("deposit_failed " + fork),
			},
		})
		return c
	}},

	{"contract_logs", bothForks, func(fork string) inputCase {
		c := caseFrame(fork, "contract_logs",
			"attributes + call to a contract emitting LOG1 x3 and one SSTORE (non-trivial bloom/logsCount)",
			defaultFeeParams(), 10_000_000)
		c.Pre[logsAddr] = types.Account{Balance: big.NewInt(0), Code: logsCode()}
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, logsAddr, big.NewInt(0), 200_000, nil))
		c.ExtraStorage = map[common.Address][]common.Hash{
			logsAddr: {common.BigToHash(big.NewInt(0))},
		}
		return c
	}},

	{"message_passer_write", bothForks, func(fork string) inputCase {
		// The L2ToL1MessagePasser gets a minimal SSTORE code so its storage
		// root -- and therefore the Isthmus withdrawalsRoot -- changes.
		c := caseFrame(fork, "message_passer_write",
			"attributes + tx writing to the L2ToL1MessagePasser (withdrawalsRoot becomes non-empty)",
			defaultFeeParams(), 10_000_000)
		c.Pre[messagePasserAddr] = types.Account{Balance: big.NewInt(0), Nonce: 1, Code: messagePasserCode}
		fund(&c, 1, eth(100))
		payload := common.HexToHash("0x000000000000000000000000000000000000000000000000000000000000beef")
		c.Transactions = append(c.Transactions, transferTx(1, 0, messagePasserAddr, big.NewInt(0), 100_000, payload[:]))
		c.ExtraStorage = map[common.Address][]common.Hash{
			messagePasserAddr: {common.BigToHash(big.NewInt(1))},
		}
		return c
	}},
	{"l1block_deposit_slots", bothForks, func(fork string) inputCase {
		// Replace the code-less L1Block (caseFrame) with the REAL 146B runtime
		// code: the L1-attributes deposit now executes l1BlockCode and writes the
		// 4 consensus slots (1/3/7/8) it actually touches. Non-default feeParams
		// (opFeeScalar/opFeeConstant/daScalar non-zero) so slot8 carries a value.
		// NOTE (scope erratum in the Task 1 brief): the 146B code writes ONLY
		// slots 1/3/7/8 -- it does NOT write slot 0 (number/timestamp) or slot 2
		// (blockhash); blockhash/sequenceNumber writes are covered at the real-node
		// layer (Task 2), t8n locks the consensus slots + stateRoot/withdrawalsRoot.
		fp := defaultFeeParams()
		fp.opFeeScalar = 0x55c6fb7c
		fp.opFeeConstant = 1256417826609331460
		fp.daScalar = 0x1234
		c := caseFrame(fork, "l1block_deposit_slots",
			"L1 attributes deposit executes real L1Block code; slots 1/3/7/8 written",
			fp, 10_000_000)
		c.Pre[l1BlockAddr] = types.Account{Balance: big.NewInt(0), Nonce: 1,
			Code: l1BlockRuntimeCode, Storage: fp.l1BlockStorage(fork)}
		c.ExtraStorage = map[common.Address][]common.Hash{
			l1BlockAddr: {
				types.L1BaseFeeSlot, types.L1FeeScalarsSlot,
				types.L1BlobBaseFeeSlot, types.OperatorFeeParamsSlot},
		}
		return c
	}},
	{"message_passer_withdraw", bothForks, func(fork string) inputCase {
		// Real initiateWithdrawal flow: the L2ToL1MessagePasser predeploy carries
		// its REAL deployed runtime code (1747B, see realMessagePasserCode), and a
		// signed EIP-1559 tx calls initiateWithdrawal(target, gasLimit, data). The
		// contract writes msgNonce (slot 1) and sentMessages[hash] (dynamic slot at
		// keccak256(hash||0)); the withdrawal hash feeds the Isthmus withdrawalsRoot.
		c := caseFrame(fork, "message_passer_withdraw",
			"real initiateWithdrawal -> MessagePassed event + message hash storage; withdrawalsRoot non-empty",
			defaultFeeParams(), 10_000_000)
		c.Pre[messagePasserAddr] = types.Account{Balance: big.NewInt(0), Nonce: 1,
			Code: realMessagePasserCode}
		fund(&c, 1, eth(100))
		// initiateWithdrawal(target=0xdead...0001, gasLimit=100000, data=0xbeef)
		// selector 0xc2b3e5ac + ABI encoding (address,uint256,bytes), 4+160=164B:
		target := common.HexToAddress("0xdead000000000000000000000000000000000001")
		calldata := make([]byte, 4+32*5)
		copy(calldata[0:4], []byte{0xc2, 0xb3, 0xe5, 0xac}) // selector
		copy(calldata[16:36], target[:])                    // target (left-pad 12B)
		binary.BigEndian.PutUint64(calldata[60:68], 100000) // gasLimit at [36:68]
		binary.BigEndian.PutUint64(calldata[92:100], 0x60)  // dataOffset at [68:100]
		binary.BigEndian.PutUint64(calldata[124:132], 2)    // dataLen at [100:132]
		copy(calldata[132:134], []byte{0xbe, 0xef})         // data at [132:134]
		c.Transactions = append(c.Transactions, transferTx(1, 0, messagePasserAddr,
			big.NewInt(0), 200_000, calldata))
		// ExtraStorage is mandatory: emitPostState hard-fails on undeclared written
		// slots. Real initiateWithdrawal (first call, msgNonce 0) writes:
		//   slot 1 = msgNonce (self-increment, becomes 1)
		//   dynamic slot keccak256(withdrawalHash ‖ be32(0)) = sentMessages[hash]=true
		// withdrawalHash = keccak256(abi.encode(versionedNonce, sender, target,
		//   value, gasLimit, data)); versionedNonce = (1<<240)|0 for the first
		//   withdrawal (this contract's encodeVersionedNonce, NOT the brief's 1).
		versionedNonce := new(big.Int).Lsh(big.NewInt(1), 240)
		withdrawalHash := crypto.Keccak256(
			abiEncodeWithdrawal(versionedNonce, addrOfKey(1), target, 0, 100000, []byte{0xbe, 0xef}))
		sentMessagesSlot := common.BytesToHash(crypto.Keccak256(withdrawalHash, make([]byte, 32)))
		c.ExtraStorage = map[common.Address][]common.Hash{
			messagePasserAddr: {
				common.BigToHash(big.NewInt(1)), // msgNonce (slot1, after first = 1)
				sentMessagesSlot,                // sentMessages dynamic slot
			},
		}
		return c
	}},

	{"tx_reverted", bothForks, func(fork string) inputCase {
		c := caseFrame(fork, "tx_reverted",
			"attributes + normal tx that REVERTs (status 0 in a valid block; value transfer rolled back)",
			defaultFeeParams(), 10_000_000)
		c.Pre[revertAddr] = types.Account{Balance: big.NewInt(0), Code: revertCode}
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, revertAddr, milliEth(100), 100_000, nil))
		return c
	}},

	{"setcode_7702", bothForks, func(fork string) inputCase {
		// Sponsor (key 1) sends the 0x04 tx to the authority (key 4), whose
		// authorization delegates to delegateAddr; the call then executes the
		// delegated code in the authority's context (SSTORE slot1=0x2a).
		c := caseFrame(fork, "setcode_7702",
			"attributes + EIP-7702 setcode tx (sponsor != authority); delegated code runs in authority context",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		authority := addrOfKey(4)
		c.Pre[authority] = types.Account{Balance: eth(1)}
		c.Pre[delegateAddr] = types.Account{Balance: big.NewInt(0), Code: delegateCode}
		authKey := privKey(4)
		sponsorKey := privKey(1)
		c.Transactions = append(c.Transactions, inputTx{
			OpType:               "setcode",
			ChainID:              hd256(chainID),
			Nonce:                hd64(0),
			To:                   &authority,
			Value:                hd256(big.NewInt(0)),
			Gas:                  hd64(200_000),
			MaxFeePerGas:         hdu(2_000_000_000),
			MaxPriorityFeePerGas: hdu(100_000_000),
			SecretKey:            &sponsorKey,
			OpAuthorizations: []inputAuthorization{{
				ChainID:       hd256(chainID),
				Address:       delegateAddr,
				Nonce:         0,
				AuthSecretKey: authKey,
			}},
		})
		c.ExtraCandidates = []common.Address{delegateAddr}
		c.ExtraStorage = map[common.Address][]common.Hash{
			authority: {common.BigToHash(big.NewInt(1))},
		}
		return c
	}},

	{"big_block_130tx", []string{"isthmus"}, func(fork string) inputCase {
		// 131 txs total (attributes + 130 transfers): receipt/tx trie keys
		// 128..130 exercise the two-byte RLP key encoding; 30M gas limit.
		c := caseFrame(fork, "big_block_130tx",
			"attributes + 130 transfers (tx/receipt trie two-byte RLP keys at indices 128+; 30M gas limit)",
			defaultFeeParams(), 30_000_000)
		fund(&c, 1, eth(100))
		for i := uint64(0); i < 130; i++ {
			c.Transactions = append(c.Transactions,
				transferTx(1, i, recA, big.NewInt(int64(1000+i)), 21_000, nil))
		}
		return c
	}},

	{"system_contracts_real", bothForks, func(fork string) inputCase {
		// Real EIP-4788/2935 bytecode (provenance: see constants above).
		// Ring slots pre-seeded STALE and asserted overwritten by the block's
		// system calls: 4788 slots time%8191 (timestamp) and +8191 (root),
		// 2935 slot (number-1)%8191 (parent hash == genesis hash).
		// Ring WRAP (Genesis.Number=8191) is NOT representable: op-geth
		// Genesis.Commit rejects number>0 (core/genesis.go:728) -- probed at
		// runtime via --probe-genesis-number; recorded in README.
		c := caseFrame(fork, "system_contracts_real",
			"real EIP-4788/2935 bytecode; pre-seeded stale ring slots overwritten by system calls",
			defaultFeeParams(), 10_000_000)
		blockTime := uint64(c.Genesis.Timestamp) + 10
		blockNumber := uint64(1)
		slot4788a := common.BigToHash(new(big.Int).SetUint64(blockTime % ringSize))
		slot4788b := common.BigToHash(new(big.Int).SetUint64(blockTime%ringSize + ringSize))
		slot2935 := common.BigToHash(new(big.Int).SetUint64((blockNumber - 1) % ringSize))
		c.Pre[beaconRootsAddr] = types.Account{
			Balance: big.NewInt(0),
			Nonce:   1,
			Code:    beaconRootsCode,
			Storage: map[common.Hash]common.Hash{
				slot4788a: common.HexToHash("0xdead000000000000000000000000000000000000000000000000000000000001"),
				slot4788b: common.HexToHash("0xdead000000000000000000000000000000000000000000000000000000000002"),
			},
		}
		c.Pre[historyStorage] = types.Account{
			Balance: big.NewInt(0),
			Nonce:   1,
			Code:    historyStorageCode,
			Storage: map[common.Hash]common.Hash{
				slot2935: common.HexToHash("0xdead000000000000000000000000000000000000000000000000000000000003"),
			},
		}
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, recA, eth(1), 21_000, nil))
		return c
	}},

	// ----- B-7 order-observable batch: +1 single-fork (isthmus) case = 34 vectors -----

	{"system_call_order_observable", []string{"isthmus"}, func(fork string) inputCase {
		// B-7 (single fork isthmus, order-observable). Same real EIP-4788/2935
		// pre-deployment as system_contracts_real (stale ring slots), PLUS a
		// user reader contract that CALLs the 4788 GET path with the block
		// timestamp and SSTOREs the returned beacon root into its own slot
		// t%8191. The 4788 GET path's ts==sload(ts) check passes only after the
		// block's system call overwrote the stale pre-seeded slot -- so the
		// stored root (and the reader tx status) is a direct probe of whether
		// the system call precedes the user tx.
		c := caseFrame(fork, "system_call_order_observable",
			"real EIP-4788/2935 bytecode + user reader that CALLs the 4788 GET path and SSTOREs the returned root (order-observable: system call must precede user txs)",
			defaultFeeParams(), 10_000_000)
		blockTime := uint64(c.Genesis.Timestamp) + 10
		blockNumber := uint64(1)
		slot4788a := common.BigToHash(new(big.Int).SetUint64(blockTime % ringSize))
		slot4788b := common.BigToHash(new(big.Int).SetUint64(blockTime%ringSize + ringSize))
		slot2935 := common.BigToHash(new(big.Int).SetUint64((blockNumber - 1) % ringSize))
		c.Pre[beaconRootsAddr] = types.Account{
			Balance: big.NewInt(0),
			Nonce:   1,
			Code:    beaconRootsCode,
			Storage: map[common.Hash]common.Hash{
				slot4788a: common.HexToHash("0xdead000000000000000000000000000000000000000000000000000000000001"),
				slot4788b: common.HexToHash("0xdead000000000000000000000000000000000000000000000000000000000002"),
			},
		}
		c.Pre[historyStorage] = types.Account{
			Balance: big.NewInt(0),
			Nonce:   1,
			Code:    historyStorageCode,
			Storage: map[common.Hash]common.Hash{
				slot2935: common.HexToHash("0xdead000000000000000000000000000000000000000000000000000000000003"),
			},
		}
		c.Pre[readerAddr] = types.Account{Balance: big.NewInt(0), Code: beaconReaderCode}
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, readerAddr, big.NewInt(0), 100_000, nil))
		c.ExtraStorage = map[common.Address][]common.Hash{
			readerAddr: {common.BigToHash(new(big.Int).SetUint64(blockTime % ringSize))},
		}
		return c
	}},

	{"first_block", []string{"jovian"}, func(fork string) inputCase {
		// Jovian activation form: Isthmus-length/selector attributes,
		// deposits-only, header blobGasUsed == an EXPLICIT 0x0.
		fp := defaultFeeParams()
		fp.isthmusLayout = true
		return caseFrame(fork, "first_block",
			"Jovian activation block: Isthmus-length attributes, deposits-only, blobGasUsed emitted as explicit 0x0",
			fp, 10_000_000)
	}},

	{"da_mix", []string{"jovian"}, func(fork string) inputCase {
		// Non-zero daFootprintGasScalar; three txs landing on the
		// estimatedDASizeScaled 100e6 FLOOR branch (plain transfer) and the
		// LINEAR branch (two calldata sizes) -- per-tx _op_da_footprint
		// mutually distinct and non-zero.
		fp := defaultFeeParams()
		fp.daScalar = 400
		c := caseFrame(fork, "da_mix",
			"Jovian DA footprint: floor-branch transfer + two linear-branch calldata txs, distinct non-zero per-tx footprints",
			fp, 10_000_000)
		fund(&c, 1, eth(100))
		fund(&c, 2, eth(100))
		fund(&c, 3, eth(100))
		c.Transactions = append(c.Transactions,
			transferTx(1, 0, recA, eth(1), 21_000, nil),
			transferTx(2, 0, recA, big.NewInt(0), 200_000, junkData("da_mix small", 300)),
			transferTx(3, 0, recA, big.NewInt(0), 400_000, junkData("da_mix large", 1200)),
		)
		return c
	}},

	{"fee_env_observer", bothForks, func(fork string) inputCase {
		// Contract SSTOREs GASPRICE/BASEFEE/SELFBALANCE; operator fee
		// scalar/constant non-zero, so the Isthmus /1e6 vs Jovian x100
		// formulas produce different vault balances and per-tx fees.
		fp := defaultFeeParams()
		fp.opFeeScalar = 5000
		fp.opFeeConstant = 7777
		c := caseFrame(fork, "fee_env_observer",
			"contract observes GASPRICE/BASEFEE/SELFBALANCE via SSTORE; non-zero operator fee params (fork-specific formula)",
			fp, 10_000_000)
		c.Pre[feeObsAddr] = types.Account{Balance: big.NewInt(0), Code: feeObserverCode}
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, feeObsAddr, milliEth(500), 200_000, nil))
		c.ExtraStorage = map[common.Address][]common.Hash{
			feeObsAddr: {
				common.BigToHash(big.NewInt(0)),
				common.BigToHash(big.NewInt(1)),
				common.BigToHash(big.NewInt(2)),
			},
		}
		return c
	}},

	{"gaslimit_observer", bothForks, func(fork string) inputCase {
		// 触发注入路径 BlockInfo.gasLimit=blockGasLeft vs 块头 gasLimit 的分叉（spec §6.1）。
		c := caseFrame(fork, "gaslimit_observer",
			"contract reads GASLIMIT and SSTOREs slot0; injected-path BlockInfo.gasLimit=blockGasLeft diverges from header gasLimit",
			defaultFeeParams(), 10_000_000)
		c.Pre[gaslimitObsAddr] = types.Account{Balance: big.NewInt(0), Code: gaslimitObserverCode}
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, gaslimitObsAddr, big.NewInt(0), 200_000, nil))
		// opt8n-ref postState 校验：contract 执行期写入的每个 slot 都必须由 corpus 声明
		// （pre storage / extra_storage）。gaslimitObserverCode 仅写 slot 0。
		c.ExtraStorage = map[common.Address][]common.Hash{
			gaslimitObsAddr: {common.BigToHash(big.NewInt(0))},
		}
		return c
	}},

	{"deposit_basefee_observer", bothForks, func(fork string) inputCase {
		// v2（D7）：绿守卫向量——不再触发分叉（Task 4 后 executeDeposit 读 header baseFee），
		// 验证 deposit 内 BASEFEE 读数三方（A/B/op-geth）一致 == header baseFee，回归保护 Task 4 修复。
		// spec §6.2 已按 v2 修订为「绿守卫语义」。
		c := caseFrame(fork, "deposit_basefee_observer",
			"deposit calls a contract reading BASEFEE; guards that injected path reads header baseFee (Task 4)",
			defaultFeeParams(), 10_000_000)
		c.Pre[basefeeObsAddr] = types.Account{Balance: big.NewInt(0), Code: basefeeObserverCode}
		to := basefeeObsAddr // 审查 R1：inputDeposit.To 是 *common.Address，必须取地址
		c.Transactions = append(c.Transactions, inputTx{
			OpType: "deposit",
			OpDeposit: &inputDeposit{
				From: userDepositor, To: &to, Gas: 200_000,
				SourceHash: sourceHash("deposit_basefee_observer " + fork),
			},
			Data: hexutil.Bytes{},
		})
		// opt8n-ref postState 校验：contract 执行期写入的每个 slot 都必须由 corpus 声明。
		// basefeeObserverCode 仅写 slot 0。
		c.ExtraStorage = map[common.Address][]common.Hash{
			basefeeObsAddr: {common.BigToHash(big.NewInt(0))},
		}
		return c
	}},

	// ----- corpus augmentation (spec rev.3): +3 cases x both forks -----

	{"contract_create", bothForks, func(fork string) inputCase {
		// Deposit-CREATE + EIP-1559 CREATE. Created addresses are pre-derived
		// (crypto.CreateAddress; the deposit with its pre-tx nonce) into
		// extra_candidates and the init-phase slot 0x0 into extra_storage --
		// the generator's receipt.ContractAddress merge is the runtime second
		// bottom of the same facts.
		c := caseFrame(fork, "contract_create",
			"attributes + deposit-CREATE + EIP-1559 CREATE; initcode SSTOREs slot0 then deploys a 6-byte runtime",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, inputTx{
			OpType: "deposit",
			OpDeposit: &inputDeposit{
				From:       userDepositor,
				To:         nil, // deposit-CREATE
				Gas:        math.HexOrDecimal64(200_000),
				SourceHash: sourceHash("contract_create " + fork),
			},
			Data: createInitCode,
		})
		k := privKey(1)
		c.Transactions = append(c.Transactions, inputTx{
			OpType:               "eip1559",
			ChainID:              hd256(chainID),
			Nonce:                hd64(0),
			Value:                hd256(big.NewInt(0)),
			Gas:                  hd64(200_000),
			MaxFeePerGas:         hdu(2_000_000_000),
			MaxPriorityFeePerGas: hdu(100_000_000),
			Data:                 createInitCode,
			SecretKey:            &k,
		})
		depositCreated := crypto.CreateAddress(userDepositor, 0) // pre-tx nonce 0
		eipCreated := crypto.CreateAddress(addrOfKey(1), 0)
		c.ExtraCandidates = []common.Address{depositCreated, eipCreated}
		c.ExtraStorage = map[common.Address][]common.Hash{
			depositCreated: {common.BigToHash(big.NewInt(0))},
			eipCreated:     {common.BigToHash(big.NewInt(0))},
		}
		return c
	}},

	{"access_list", bothForks, func(fork string) inputCase {
		// tx1 carries an EIP-2930 access list against the SLOAD target:
		// slot0 listed+accessed, slot5 listed-not-accessed, slot2
		// accessed-NOT-listed (cold-price discriminator), plus a
		// listed-but-never-called address with an empty key list. tx2 is the
		// same call WITHOUT a list (narrowed cancellation-surface control).
		c := caseFrame(fork, "access_list",
			"attributes + EIP-2930 access-list call (listed/unlisted slot mix) + identical no-list control call",
			defaultFeeParams(), 10_000_000)
		c.Pre[aclAddr] = types.Account{
			Balance: big.NewInt(0),
			Code:    aclCode,
			Storage: map[common.Hash]common.Hash{
				common.BigToHash(big.NewInt(0)): common.BigToHash(big.NewInt(0xa0)),
				common.BigToHash(big.NewInt(2)): common.BigToHash(big.NewInt(0xa2)),
			},
		}
		fund(&c, 1, eth(100))
		fund(&c, 2, eth(100))
		k1 := privKey(1)
		target := aclAddr
		c.Transactions = append(c.Transactions, inputTx{
			OpType:               "eip1559",
			ChainID:              hd256(chainID),
			Nonce:                hd64(0),
			To:                   &target,
			Value:                hd256(big.NewInt(0)),
			Gas:                  hd64(100_000),
			MaxFeePerGas:         hdu(2_000_000_000),
			MaxPriorityFeePerGas: hdu(100_000_000),
			AccessList: []outputAccessTuple{
				{Address: aclAddr, StorageKeys: []common.Hash{
					common.BigToHash(big.NewInt(0)),
					common.BigToHash(big.NewInt(5)),
				}},
				{Address: aclListedOnly, StorageKeys: []common.Hash{}},
			},
			SecretKey: &k1,
		},
			transferTx(2, 0, aclAddr, big.NewInt(0), 100_000, nil),
		)
		return c
	}},

	{"setcode_7702_skips", bothForks, func(fork string) inputCase {
		// Four-tuple skip matrix: [valid, chainId=9999 (signed), nonce=99
		// (signed), raw-override rawYParity=5 (marked structurally
		// unrecoverable)]. The valid tuple's applied delegation code on the
		// authority is the postState byte-for-byte anchor (Task 1 rule (3));
		// the three skipped tuples still cost 7702 intrinsic gas x4 -- a
		// discarded marked tuple would show up as receipts[i].gasUsed red.
		c := caseFrame(fork, "setcode_7702_skips",
			"attributes + EIP-7702 tx with non-empty access list and 4 tuples: valid + chainId-mismatch + nonce-mismatch + structurally-unrecoverable (marked)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		authority := addrOfKey(4)
		c.Pre[authority] = types.Account{Balance: eth(1)}
		c.Pre[delegateAddr] = types.Account{Balance: big.NewInt(0), Code: delegateCode}
		sponsorKey := privKey(1)
		authKey4 := privKey(4)
		authKey5 := privKey(5)
		authKey6 := privKey(6)
		rawY := hexutil.Uint64(5)
		rawOne := (*hexutil.Big)(big.NewInt(1))
		c.Transactions = append(c.Transactions, inputTx{
			OpType:               "setcode",
			ChainID:              hd256(chainID),
			Nonce:                hd64(0),
			To:                   &authority,
			Value:                hd256(big.NewInt(0)),
			Gas:                  hd64(300_000),
			MaxFeePerGas:         hdu(2_000_000_000),
			MaxPriorityFeePerGas: hdu(100_000_000),
			AccessList: []outputAccessTuple{
				{Address: delegateAddr, StorageKeys: []common.Hash{
					common.BigToHash(big.NewInt(0)),
				}},
			},
			SecretKey: &sponsorKey,
			OpAuthorizations: []inputAuthorization{
				{ // applies: delegation anchor on the authority
					ChainID:       hd256(chainID),
					Address:       delegateAddr,
					Nonce:         0,
					AuthSecretKey: authKey4,
				},
				{ // skipped: chain-id mismatch (signature itself valid)
					ChainID:       hd256(big.NewInt(9999)),
					Address:       delegateAddr,
					Nonce:         0,
					AuthSecretKey: authKey5,
				},
				{ // skipped: authority nonce mismatch (signature itself valid)
					ChainID:       hd256(chainID),
					Address:       delegateAddr,
					Nonce:         99,
					AuthSecretKey: authKey6,
				},
				{ // skipped: raw override, structurally unrecoverable (marked)
					ChainID:             hd256(chainID),
					Address:             delegateAddr,
					Nonce:               0,
					SignerUnrecoverable: true,
					RawYParity:          &rawY,
					RawR:                rawOne,
					RawS:                rawOne,
				},
			},
		})
		c.ExtraCandidates = []common.Address{delegateAddr}
		c.ExtraStorage = map[common.Address][]common.Hash{
			authority: {common.BigToHash(big.NewInt(1))},
		}
		return c
	}},

	// ----- defer-sweep batch: +1 case x both forks = 33 vectors -----

	{"empty_account_cleanup", bothForks, func(fork string) inputCase {
		// EIP-161 touch-delete corpus case. Pre seeds an exists-but-empty
		// account (balance 0, nonce 0, no code, no storage): genesis
		// flushAlloc commits with deleteEmptyObjects=false (core/genesis.go),
		// so the account IS materialized in the genesis trie (probed against
		// op-geth @ e8800cffe: Exist=true/Empty=true at genesis, and the
		// genesis root differs from the same alloc without it). A zero-value
		// 21000-gas CALL touches it; at block commit (EIP-158 active) it is
		// deleted, so op-geth's postState emits the vanished candidate as the
		// trie-semantic zero account {"balance":"0x0"}.
		// Honest gate boundary: the replay's postState comparison
		// canonicalizes empty == absent (OpT8nReplayTest.cpp postState
		// rules), so a replay that DROPPED the delete would still stay green
		// on this vector -- it is NOT a delete-vs-keep discriminator on its
		// own. The deletion is pinned instead by (a) the gold side (op-geth
		// InsertChain self-check generated this vector from a state where
		// the delete provably happens), (b) the tx-level unit test
		// OpStateDiffSanitize.RealEmptyAccountDeleteSurvivesSanitize, and
		// (c) the block-level GTest
		// OpStateDiffSanitize.BlockLevelRealDeleteSurvivesSanitize.
		c := caseFrame(fork, "empty_account_cleanup",
			"attributes + zero-value CALL touching a pre-seeded exists-but-empty account (EIP-161 touch-delete; postState emits the zero account)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Pre[emptyTouchAddr] = types.Account{Balance: big.NewInt(0)}
		c.Transactions = append(c.Transactions,
			transferTx(1, 0, emptyTouchAddr, big.NewInt(0), 21_000, nil))
		return c
	}},

	// ----- Phase 2 line A: formula-boundary cases (Task 3 Step 1) -----
	// Ecotone/Fjord run at EVMC_CANCUN (PragueTime nilled together with
	// IsthmusTime): NO 7702/setcode arm, rev = CANCUN. The transfer carries
	// calldata so the L1 formula is meaningfully exercised (a data-less
	// transfer would sit on the FastLZ/Ecotone floor and not discriminate).

	{"transfer_basic", []string{"ecotone"}, func(fork string) inputCase {
		c := caseFrame(fork, "transfer_basic",
			"Ecotone formula boundary: attributes + EIP-1559 transfer with calldata (activates the Ecotone calldata-gas L1 formula; l1_gas_used > floor)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, recA, eth(1), 100_000, junkData("ecotone_transfer_basic", 200)))
		return c
	}},

	{"contract_create", []string{"ecotone"}, func(fork string) inputCase {
		// EIP-1559 CREATE: the initcode IS the tx calldata, so the Ecotone
		// calldata formula charges against it; created address pre-derived into
		// extra_candidates and its init-phase slot 0x0 into extra_storage.
		c := caseFrame(fork, "contract_create",
			"Ecotone formula boundary: attributes + EIP-1559 CREATE (initcode calldata exercises the Ecotone L1 formula)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		k := privKey(1)
		c.Transactions = append(c.Transactions, inputTx{
			OpType:               "eip1559",
			ChainID:              hd256(chainID),
			Nonce:                hd64(0),
			Value:                hd256(big.NewInt(0)),
			Gas:                  hd64(200_000),
			MaxFeePerGas:         hdu(2_000_000_000),
			MaxPriorityFeePerGas: hdu(100_000_000),
			Data:                 createInitCode,
			SecretKey:            &k,
		})
		eipCreated := crypto.CreateAddress(addrOfKey(1), 0)
		c.ExtraCandidates = []common.Address{eipCreated}
		c.ExtraStorage = map[common.Address][]common.Hash{
			eipCreated: {common.BigToHash(big.NewInt(0))},
		}
		return c
	}},

	{"transfer_basic", []string{"fjord"}, func(fork string) inputCase {
		c := caseFrame(fork, "transfer_basic",
			"Fjord formula boundary: attributes + EIP-1559 transfer with calldata (activates the FastLZ linear-regression L1 formula; l1_gas_used via estimatedDASizeScaled)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, recA, eth(1), 100_000, junkData("fjord_transfer_basic", 200)))
		return c
	}},

	{"contract_create", []string{"fjord"}, func(fork string) inputCase {
		c := caseFrame(fork, "contract_create",
			"Fjord formula boundary: attributes + EIP-1559 CREATE (initcode calldata exercises the FastLZ L1 formula)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		k := privKey(1)
		c.Transactions = append(c.Transactions, inputTx{
			OpType:               "eip1559",
			ChainID:              hd256(chainID),
			Nonce:                hd64(0),
			Value:                hd256(big.NewInt(0)),
			Gas:                  hd64(200_000),
			MaxFeePerGas:         hdu(2_000_000_000),
			MaxPriorityFeePerGas: hdu(100_000_000),
			Data:                 createInitCode,
			SecretKey:            &k,
		})
		eipCreated := crypto.CreateAddress(addrOfKey(1), 0)
		c.ExtraCandidates = []common.Address{eipCreated}
		c.ExtraStorage = map[common.Address][]common.Hash{
			eipCreated: {common.BigToHash(big.NewInt(0))},
		}
		return c
	}},

	// ----- Phase 2 line A: upgrade-boundary cases (Task 3 Step 2, spec A2) -----
	// chainConfigSpec expressed via _info.activations (base = _info.hardfork).
	// 10-second coupling: genesis timestamp 1000 (= T-5), blockTime 1010
	// (= genesis+10 = T+5) -- the single block is the FIRST block of the new
	// fork (1000 < 1005 <= 1010 holds).

	{"upgrade_fjord_activation", []string{"ecotone"}, func(fork string) inputCase {
		// L1 formula switch: Ecotone calldata formula -> Fjord FastLZ. The
		// block (timestamp 1010) is Fjord; l1_gas_used reflects the FastLZ
		// estimatedDASizeScaled model instead of bedrockCalldataGasUsed.
		c := upgradeFrame("ecotone", "upgrade_fjord_activation",
			"upgrade boundary ecotone->fjord: genesis Ecotone, single block crosses FjordTime; calldataGas -> FastLZ formula switch observable in l1_gas_used",
			defaultFeeParams(), 10_000_000, "fjord", 1005)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, recA, eth(1), 100_000, junkData("upgrade_fjord_activation", 200)))
		return c
	}},

	{"upgrade_isthmus_activation", []string{"fjord"}, func(fork string) inputCase {
		// Operator fee introduced: genesis Fjord, single block crosses
		// IsthmusTime. Non-zero operator fee params -> the operator fee is
		// charged for the first time (gasUsed*scalar/1e6 + constant, Isthmus
		// formula), and the receipt emits the operator field group.
		fp := defaultFeeParams()
		fp.opFeeScalar = 5000
		fp.opFeeConstant = 7777
		c := upgradeFrame("fjord", "upgrade_isthmus_activation",
			"upgrade boundary fjord->isthmus: genesis Fjord, single block crosses IsthmusTime; operator fee first charged (Isthmus formula, non-zero scalar/constant)",
			fp, 10_000_000, "isthmus", 1005)
		fund(&c, 1, eth(100))
		c.Transactions = append(c.Transactions, transferTx(1, 0, recA, eth(1), 100_000, junkData("upgrade_isthmus_activation", 64)))
		return c
	}},

	{"upgrade_jovian_activation", []string{"isthmus"}, func(fork string) inputCase {
		// DA fields appear: genesis Isthmus, single block crosses JovianTime.
		// Deposits-ONLY + Isthmus-length/selector attributes (fp.isthmusLayout):
		// op-geth CalcDAFootprint (rollup_cost.go:568-575) hard-requires this
		// shape for the first Jovian block (176B -> no non-deposit txs -> DA
		// footprint 0). Header BlobGasUsed = 0.
		fp := defaultFeeParams()
		fp.isthmusLayout = true
		c := upgradeFrame("isthmus", "upgrade_jovian_activation",
			"upgrade boundary isthmus->jovian: genesis Isthmus, single block crosses JovianTime; deposits-only with Isthmus-length attributes (CalcDAFootprint activation form), DA fields appear in header semantics",
			fp, 10_000_000, "jovian", 1005)
		return c
	}},

	// ----- Phase 2 line B: precompile vectors (Task 1, 13 cases) -----
	// Step 1: base-7 normal vectors (fork=isthmus, tx gas 500_000). Each input
	// is genuinely valid (probe-confirmed) so the precompile returns success.
	basePrecompileCase("precompile_ecrecover",
		"ecrecover (0x01): real secp256k1 signature recovering to key 1; hash = sha256(\"fisco line-b ecrecover\")",
		preEcRecover, validEcrecoverInput),
	basePrecompileCase("precompile_sha256",
		"sha256 (0x02): 4-byte input 0xdeadbeef",
		preSha256, deadbeefInput),
	basePrecompileCase("precompile_ripemd160",
		"ripemd160 (0x03): 4-byte input 0xdeadbeef",
		preRipemd160, deadbeefInput),
	basePrecompileCase("precompile_identity",
		"identity (0x04): 4-byte input 0xdeadbeef (returns it verbatim)",
		preIdentity, deadbeefInput),
	basePrecompileCase("precompile_expmod",
		"modexp (0x05): 96B zero-header (baseLen=expLen=modLen=0) -> success; empty output (EIP-198 baseLen==modLen==0 rule)",
		preExpMod, expmodZeroHeader),
	basePrecompileCase("precompile_blake2f",
		"blake2f (0x09): EIP-152 vector 5 (rounds=12, h=IV^0x01010040, m=\"abc\", final=1)",
		preBlake2f, validBlake2fInput),
	basePrecompileCase("precompile_point_evaluation",
		"kzg point evaluation (0x0a): valid EIP-4844 proof (192B, validKZGInput)",
		prePointEval, validKZGInput()),

	// Step 2: bn256 add/mul normal vectors (fork=isthmus, tx gas 500_000).
	basePrecompileCase("precompile_bn256add",
		"bn256 add (0x06): two valid G1 points (128B input; RequiredGas 150)",
		preBn256Add, bn256AddInput),
	basePrecompileCase("precompile_bn256mul",
		"bn256 scalar mul (0x07): valid G1 + scalar 0x02 (96B input; RequiredGas 6000)",
		preBn256Mul, bn256MulInput),

	// Step 3: bn256 pairing 4-cap matrix (cap discriminators, block gas 30M).
	{"precompile_bn256pair_norm", []string{"isthmus"}, func(fork string) inputCase {
		// One valid pair (192B): well under the granite cap (112687) that is
		// active at isthmus; pairing check runs and returns (status 1).
		return precompileFrame(fork, "precompile_bn256pair_norm",
			"bn256 pairing (0x08): one valid pair (192B) succeeds at isthmus (granite-cap active, 192 << 112687)",
			[]inputTx{precompileCallTx(addrBytes(preBn256Pairing), repeatedBn256Pair(1), 500_000, 0)},
			10_000_000).build(fork)
	}},
	{"precompile_bn256pair_overcap", []string{"granite"}, func(fork string) inputCase {
		// 587 pairs = 112704B > granite cap 112687 -> bn256PairingGranite.Run
		// returns errBadPairingInputSize AFTER charging RequiredGas (20,003,000)
		// -> status 0, ALL call gas consumed (gasUsed = tx gas). tx gas clears
		// intrinsic + RequiredGas so the CAP check (not an OOG) is what fires.
		return precompileFrame(fork, "precompile_bn256pair_overcap",
			"bn256 pairing over granite cap: 587 pairs (112704B > 112687) rejected with errBadPairingInputSize after charging RequiredGas",
			[]inputTx{precompileCallTx(addrBytes(preBn256Pairing), repeatedBn256Pair(587), bn256PairOvercapGas587, 0)},
			30_000_000).build(fork)
	}},
	{"precompile_bn256pair_overcap", []string{"jovian"}, func(fork string) inputCase {
		// 428 pairs = 82176B > jovian cap 81984 (and <= granite's 112687):
		// DISCRIMINATES jovian's tighter cap from granite's. Same cap-path
		// semantics as granite (errBadPairingInputSize, status 0, all gas).
		return precompileFrame(fork, "precompile_bn256pair_overcap",
			"bn256 pairing over jovian cap: 428 pairs (82176B > 81984, <= granite 112687) rejected with errBadPairingInputSize (tighter cap than granite)",
			[]inputTx{precompileCallTx(addrBytes(preBn256Pairing), repeatedBn256Pair(428), bn256PairOvercapGas428, 0)},
			30_000_000).build(fork)
	}},
	{"precompile_bn256pair_large_success", []string{"fjord"}, func(fork string) inputCase {
		// No cap at fjord (bn256PairingIstanbul): 587 VALID pairs SUCCEED
		// (status 1, 32B output). Proves the 112687B cap appears only at
		// granite+ (same input as the granite over-cap case diverges).
		return precompileFrame(fork, "precompile_bn256pair_large_success",
			"bn256 pairing large input: 587 valid pairs (112704B) SUCCEED at fjord (no cap yet; granite+ introduces the 112687B cap)",
			[]inputTx{precompileCallTx(addrBytes(preBn256Pairing), repeatedBn256Pair(587), bn256PairOvercapGas587, 0)},
			30_000_000).build(fork)
	}},
}

// vectorName maps a spec x fork to the checked-in file base name (matches
// vectors/manifest.txt; note the two Jovian-only cases keep their plan names
// jovian_first_block / jovian_da_mix).
func vectorName(fork, name string) string {
	return fork + "_" + name
}

// emitableSpecs returns the caseSpecs that --write-cases emits: the shared
// table minus bScopeSpecs entries (Task 3 F1: legacy_transfer must not be
// emitted until the T8n replayer has a legacy consumer arm).
func emitableSpecs() []caseSpec {
	out := make([]caseSpec, 0, len(caseSpecs))
	for _, spec := range caseSpecs {
		if bScopeSpecs[spec.name] {
			continue
		}
		out = append(out, spec)
	}
	return out
}

func emitCases(dir string) error {
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	count := 0
	for _, spec := range emitableSpecs() {
		for _, fork := range spec.forks {
			c := spec.build(fork)
			raw, err := json.MarshalIndent(&c, "", "  ")
			if err != nil {
				return err
			}
			raw = append(raw, '\n')
			path := filepath.Join(dir, vectorName(fork, spec.name)+".in.json")
			if err := os.WriteFile(path, raw, 0o644); err != nil {
				return err
			}
			count++
		}
	}
	fmt.Printf("wrote %d case files to %s\n", count, dir)
	return nil
}

// ---------------------------------------------------------------------
// §4b invalid-tx kinds (Task 4). INDEPENDENT table — MUST NOT enter the shared
// caseSpecs/emitCases (review R7): GenerateChainWithGenesis.AddTx rejects an
// invalid tx outright, so a shared entry would make --write-cases emit an
// .in.json the valid pipeline cannot regenerate. The invalid-tx generation
// path (buildInvalidTxBlock) hand-rolls txRoot/blockHash instead.
// ---------------------------------------------------------------------

// invalidTxBuilder builds the invalid non-deposit tx (a *types.Transaction)
// AND its block.transactions structured output object (the replayer's
// loadBlockContext three-arm loader). The output object is authoritative: the
// replayer reads the STRUCTURED fields to build the evmone tx and keeps _op_raw
// as the signed EIP-2718 envelope for opValidate's L1-cost derivation.
type invalidTxBuilder func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error)

// invalidTxCaseSpec describes one §4b kind. consumer follows review R16:
// decode-level kinds (blob) are consumer:"both" (engine decode message is the
// validation_error_contains anchor); every other kind is consumer:"executor"
// (the engine RTTI-bypass collapses the tx-level message to a generic one, so
// the T8n throw assertion is the ONLY transaction-level semantic anchor).
type invalidTxCaseSpec struct {
	kind     string
	forks    []string
	consumer string // "executor" | "both"
	opGeth   string // op-geth rejection message substring (weak anchor)
	t8n      string // T8n throw message substring (validation_error_contains)
	decode   string // FISCO raw-tx decode message (decode-level kinds only; blob)
	build    func(fork string) (inputCase, invalidTxBuilder)
}

// invalidTxKinds returns the kind names for diagnostics.
func invalidTxKinds() []string {
	out := make([]string, len(invalidTxCaseSpecs))
	for i := range invalidTxCaseSpecs {
		out[i] = invalidTxCaseSpecs[i].kind
	}
	return out
}

// ptrBytes returns a *hexutil.Bytes for an inputTx SecretKey.
func ptrBytes(b hexutil.Bytes) *hexutil.Bytes { return &b }

var invalidTxCaseSpecs = []invalidTxCaseSpec{
	// intrinsic_gas: EIP-1559 value transfer with gas=0 < 21000 intrinsic.
	{"intrinsic_gas", bothForks, "executor", "intrinsic gas too low", "intrinsic gas too low", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_intrinsic_gas",
			"deposit + gas=0 EIP-1559 tx (intrinsic gas too low)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			k := privKey(1)
			to := recA
			in := inputTx{
				OpType: "eip1559", ChainID: hd256(chainID), Nonce: hd64(0), To: &to,
				Value: hdu(0), Gas: hd64(0), // gas=0 → intrinsic gas too low
				MaxFeePerGas: hdu(2_000_000_000), MaxPriorityFeePerGas: hdu(100_000_000),
				Data: nil, SecretKey: &k,
			}
			return buildTx(&in, signer, cfg)
		}
	}},

	// nonce_low: tx nonce 0 < account nonce 1 (pre-state raises the nonce).
	{"nonce_low", bothForks, "executor", "nonce too low", "nonce too low", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_nonce_low",
			"deposit + nonce-0 EIP-1559 tx against account nonce 1 (nonce too low)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		c.Pre[addrOfKey(1)] = types.Account{Balance: eth(100), Nonce: 1}
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			k := privKey(1)
			to := recA
			in := inputTx{
				OpType: "eip1559", ChainID: hd256(chainID), Nonce: hd64(0), To: &to,
				Value: hdu(0), Gas: hd64(21_000),
				MaxFeePerGas: hdu(2_000_000_000), MaxPriorityFeePerGas: hdu(100_000_000),
				Data: nil, SecretKey: &k,
			}
			return buildTx(&in, signer, cfg)
		}
	}},

	// nonce_high: tx nonce 5 > account nonce 0.
	{"nonce_high", bothForks, "executor", "nonce too high", "nonce too high", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_nonce_high",
			"deposit + nonce-5 EIP-1559 tx against account nonce 0 (nonce too high)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			k := privKey(1)
			to := recA
			in := inputTx{
				OpType: "eip1559", ChainID: hd256(chainID), Nonce: hd64(5), To: &to,
				Value: hdu(0), Gas: hd64(21_000),
				MaxFeePerGas: hdu(2_000_000_000), MaxPriorityFeePerGas: hdu(100_000_000),
				Data: nil, SecretKey: &k,
			}
			return buildTx(&in, signer, cfg)
		}
	}},

	// insufficient_funds: balance 1 ETH < value 2 ETH (+ gas + L1 cost).
	{"insufficient_funds", bothForks, "executor", "insufficient funds for gas * price + value", "insufficient funds for gas * price + value", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_insufficient_funds",
			"deposit + 2-ETH value transfer from a 1-ETH account (insufficient funds)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(1))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			k := privKey(1)
			to := recA
			in := inputTx{
				OpType: "eip1559", ChainID: hd256(chainID), Nonce: hd64(0), To: &to,
				Value: hd256(eth(2)), Gas: hd64(21_000),
				MaxFeePerGas: hdu(2_000_000_000), MaxPriorityFeePerGas: hdu(100_000_000),
				Data: nil, SecretKey: &k,
			}
			return buildTx(&in, signer, cfg)
		}
	}},

	// fee_cap_low: maxFeePerGas 0.5 gwei < baseFee 1 gwei (London preCheck).
	{"fee_cap_low", bothForks, "executor", "max fee per gas less than block base fee", "max fee per gas less than block base fee", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_fee_cap_low",
			"deposit + 0.5-gwei maxFeePerGas tx under 1-gwei base fee (fee cap less than base fee)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			k := privKey(1)
			to := recA
			in := inputTx{
				OpType: "eip1559", ChainID: hd256(chainID), Nonce: hd64(0), To: &to,
				Value: hdu(0), Gas: hd64(21_000),
				MaxFeePerGas: hdu(500_000_000), MaxPriorityFeePerGas: hdu(100_000_000),
				Data: nil, SecretKey: &k,
			}
			return buildTx(&in, signer, cfg)
		}
	}},

	// sender_no_eoa: sender address carries code (EIP-3607).
	{"sender_no_eoa", bothForks, "executor", "sender not an eoa", "sender not an eoa", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_sender_no_eoa",
			"deposit + EIP-1559 tx from a contract account (sender not an eoa)",
			defaultFeeParams(), 10_000_000)
		c.Pre[addrOfKey(1)] = types.Account{Balance: eth(100), Code: revertCode}
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			k := privKey(1)
			to := recA
			in := inputTx{
				OpType: "eip1559", ChainID: hd256(chainID), Nonce: hd64(0), To: &to,
				Value: hdu(0), Gas: hd64(21_000),
				MaxFeePerGas: hdu(2_000_000_000), MaxPriorityFeePerGas: hdu(100_000_000),
				Data: nil, SecretKey: &k,
			}
			return buildTx(&in, signer, cfg)
		}
	}},

	// setcode_create: EIP-7702 create (ErrSetCodeTxCreate). A real signed
	// SetCodeTx envelope cannot express nil To (To() is always non-nil), so the
	// STRUCTURED tx carries to:null (→ evmone CREATE_SET_CODE_TX) while the
	// _op_raw envelope is a setcode tx to the zero address. op_geth anchor is
	// documentation-only (the error is not reachable through a real envelope).
	{"setcode_create", bothForks, "executor", "EIP-7702 transaction cannot be used to create contract", "set code transaction must not be a create transaction", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_setcode_create",
			"deposit + EIP-7702 create tx (structured to:null → set code tx must not be a create)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			prv, from, err := parseKey(ptrBytes(privKey(1)))
			if err != nil {
				return nil, nil, err
			}
			auth, err := types.SignSetCode(prv, types.SetCodeAuthorization{
				ChainID: *uint256.MustFromBig(chainID),
				Address: recA,
				Nonce:   0,
			})
			if err != nil {
				return nil, nil, fmt.Errorf("setcode_create: SignSetCode: %w", err)
			}
			tx := types.MustSignNewTx(prv, signer, &types.SetCodeTx{
				ChainID:   uint256.MustFromBig(chainID),
				Nonce:     0,
				GasTipCap: uint256.MustFromBig(big.NewInt(100_000_000)),
				GasFeeCap: uint256.MustFromBig(big.NewInt(2_000_000_000)),
				Gas:       100_000,
				To:        common.Address{}, // envelope can't express nil To; structured to:null carries the create
				Value:     uint256.NewInt(0),
				Data:      nil,
				AuthList:  []types.SetCodeAuthorization{auth},
			})
			rawBin, err := tx.MarshalBinary()
			if err != nil {
				return nil, nil, err
			}
			outJSON, err := json.Marshal(outputSetCodeTxCreate{
				OpType:  "setcode",
				OpRaw:   hexutil.Encode(rawBin),
				ChainID: (*math.HexOrDecimal256)(chainID), Nonce: math.HexOrDecimal64(0),
				To:                   nil, // → replayer builds to:nullopt → CREATE_SET_CODE_TX
				Gas:                  math.HexOrDecimal64(100_000),
				MaxFeePerGas:         (*math.HexOrDecimal256)(big.NewInt(2_000_000_000)),
				MaxPriorityFeePerGas: (*math.HexOrDecimal256)(big.NewInt(100_000_000)),
				Value:                (*math.HexOrDecimal256)(big.NewInt(0)),
				Data:                 hexutil.Bytes{},
				Sender:               from,
				OpAuthorizationList: []outputAuthorization{{
					ChainID: (*math.HexOrDecimal256)(chainID),
					Address: recA,
					Nonce:   math.HexOrDecimal64(0),
					YParity: math.HexOrDecimal64(auth.V),
					R:       (*math.HexOrDecimal256)(auth.R.ToBig()),
					S:       (*math.HexOrDecimal256)(auth.S.ToBig()),
				}},
			})
			return tx, outJSON, err
		}
	}},

	// empty_auth_list: EIP-7702 tx with an EMPTY authorization list.
	{"empty_auth_list", bothForks, "executor", "EIP-7702 transaction with empty auth list", "empty authorization list", "", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_empty_auth_list",
			"deposit + EIP-7702 tx with empty auth list (empty authorization list)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			prv, from, err := parseKey(ptrBytes(privKey(1)))
			if err != nil {
				return nil, nil, err
			}
			tx := types.MustSignNewTx(prv, signer, &types.SetCodeTx{
				ChainID:   uint256.MustFromBig(chainID),
				Nonce:     0,
				GasTipCap: uint256.MustFromBig(big.NewInt(100_000_000)),
				GasFeeCap: uint256.MustFromBig(big.NewInt(2_000_000_000)),
				Gas:       100_000,
				To:        recA,
				Value:     uint256.NewInt(0),
				Data:      nil,
				AuthList:  []types.SetCodeAuthorization{},
			})
			rawBin, err := tx.MarshalBinary()
			if err != nil {
				return nil, nil, err
			}
			outJSON, err := json.Marshal(outputSetCodeTx{
				OpType:  "setcode",
				OpRaw:   hexutil.Encode(rawBin),
				ChainID: (*math.HexOrDecimal256)(chainID), Nonce: math.HexOrDecimal64(0),
				To:                   recA,
				Gas:                  math.HexOrDecimal64(100_000),
				MaxFeePerGas:         (*math.HexOrDecimal256)(big.NewInt(2_000_000_000)),
				MaxPriorityFeePerGas: (*math.HexOrDecimal256)(big.NewInt(100_000_000)),
				Value:                (*math.HexOrDecimal256)(big.NewInt(0)),
				Data:                 hexutil.Bytes{},
				Sender:               from,
				OpAuthorizationList:  []outputAuthorization{},
			})
			return tx, outJSON, err
		}
	}},

	// blob: type-3 blob tx. op-geth OP rejects at txpool ("transaction type
	// not supported") and at block validation ("data blobs present in block
	// body"); FISCO rejects at raw-tx DECODE ("unsupported tx type byte 0x03").
	// consumer:"both" (review R16) — the decode message is a reliable engine
	// validationError surface.
	{"blob", bothForks, "both", "data blobs present in block body", "unsupported tx type byte 0x03", "unsupported tx type byte 0x03", func(fork string) (inputCase, invalidTxBuilder) {
		c := caseFrame(fork, "invalid_blob",
			"deposit + type-3 blob tx (OP rejects blobs; FISCO decode fails)",
			defaultFeeParams(), 10_000_000)
		fund(&c, 1, eth(100))
		return c, func(signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
			prv, from, err := parseKey(ptrBytes(privKey(1)))
			if err != nil {
				return nil, nil, err
			}
			blobHash := common.HexToHash("0x0101010101010101010101010101010101010101010101010101010101010101")
			// The OP Isthmus signer EXPLICITLY unsets BlobTxType (it is an
			// invalid-tx kind on OP chains), so sign the envelope with the
			// Cancun signer instead — the tx is rejected at decode/block
			// validation before signature validity is ever consulted.
			blobSigner := types.NewCancunSigner(chainID)
			tx := types.MustSignNewTx(prv, blobSigner, &types.BlobTx{
				ChainID:    uint256.MustFromBig(chainID),
				Nonce:      0,
				GasTipCap:  uint256.MustFromBig(big.NewInt(100_000_000)),
				GasFeeCap:  uint256.MustFromBig(big.NewInt(2_000_000_000)),
				Gas:        100_000,
				To:         recA,
				Value:      uint256.NewInt(0),
				Data:       nil,
				BlobFeeCap: uint256.MustFromBig(big.NewInt(1_000_000_000)),
				BlobHashes: []common.Hash{blobHash},
				Sidecar:    nil, // block body blob txs must NOT carry a sidecar (block_validator.go:99)
			})
			rawBin, err := tx.MarshalBinary()
			if err != nil {
				return nil, nil, err
			}
			outJSON, err := json.Marshal(outputBlobTx{
				OpType:  "blob",
				OpRaw:   hexutil.Encode(rawBin),
				ChainID: (*math.HexOrDecimal256)(chainID), Nonce: math.HexOrDecimal64(0),
				To:                   &recA,
				Gas:                  math.HexOrDecimal64(100_000),
				MaxFeePerGas:         (*math.HexOrDecimal256)(big.NewInt(2_000_000_000)),
				MaxPriorityFeePerGas: (*math.HexOrDecimal256)(big.NewInt(100_000_000)),
				BlobFeeCap:           (*math.HexOrDecimal256)(big.NewInt(1_000_000_000)),
				BlobHashes:           []common.Hash{blobHash},
				Value:                (*math.HexOrDecimal256)(big.NewInt(0)),
				Data:                 hexutil.Bytes{},
				Sender:               from,
			})
			return tx, outJSON, err
		}
	}},
}
