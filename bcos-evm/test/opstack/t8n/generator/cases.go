// Corpus definitions for the M-B3+M6 block-level vector matrix (plan Step 2
// table, 14 cases x fork column = 25 vectors; corpus augmentation spec rev.3
// adds 3 cases x both forks = 31 vectors; the defer-sweep batch adds
// empty_account_cleanup x both forks = 33 vectors; the B-7 order-observable
// batch adds 1 single-fork (isthmus) case = 34 vectors). `opt8n-ref --write-cases <dir>`
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
)

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
	// empty_account_cleanup case: pre-seeded exists-but-empty account
	// (balance 0, nonce 0, no code, no storage). NOT a precompile on either
	// side (exact-set membership; op-stack P256VERIFY sits at 0x…0100).
	emptyTouchAddr = common.HexToAddress("0x00000000000000000000000000000000000000e0")

	// PUSH0 PUSH0 REVERT
	revertCode = hexutil.MustDecode("0x5f5ffd")
	// SSTORE(0, 0x2a); LOG1 x3 (empty data, distinct topics); STOP
	// (assembled below in logsCode()).
	// SSTORE(1, CALLDATALOAD(0)); STOP
	messagePasserCode = hexutil.MustDecode("0x5f3560015500")
	// SSTORE(1, 0x2a); STOP -- EIP-7702 delegate target
	delegateCode = hexutil.MustDecode("0x602a60015500")
	// SSTORE(0, GASPRICE); SSTORE(1, BASEFEE); SSTORE(2, SELFBALANCE); STOP
	feeObserverCode = hexutil.MustDecode("0x3a5f55486001554760025500")
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
		tail := hexutil.MustDecode("0x5af15060205f5f3e5f51905500")        // GAS CALL POP RDC PUSH0 MLOAD SWAP1 SSTORE STOP
		out := append(head, params.BeaconRootsAddress.Bytes()...)
		return append(out, tail...)
	}()
)

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

func (fp feeParams) attributesData(jovianCfg bool) hexutil.Bytes {
	size := types.IsthmusL1AttributesLen
	jovianLayout := jovianCfg && !fp.isthmusLayout
	if jovianLayout {
		size = types.JovianL1AttributesLen
	}
	buf := make([]byte, size)
	if jovianLayout {
		copy(buf[0:4], types.JovianL1AttributesSelector)
		binary.BigEndian.PutUint16(buf[176:178], fp.daScalar)
	} else {
		copy(buf[0:4], types.IsthmusL1AttributesSelector)
	}
	binary.BigEndian.PutUint32(buf[4:8], fp.baseFeeScalar)
	binary.BigEndian.PutUint32(buf[8:12], fp.blobBaseFeeScalar)
	// [12:20] sequenceNumber, [20:28] l1 timestamp, [28:36] l1 number: zero
	// (not read by any cost function; not slot-mirrored -- see README).
	fp.l1BaseFee.FillBytes(buf[36:68])
	fp.blobBaseFee.FillBytes(buf[68:100])
	// [100:132] l1 block hash, [132:164] batcher hash: zero.
	binary.BigEndian.PutUint32(buf[164:168], fp.opFeeScalar)
	binary.BigEndian.PutUint64(buf[168:176], fp.opFeeConstant)
	return buf
}

func (fp feeParams) l1BlockStorage(jovianCfg bool) map[common.Hash]common.Hash {
	st := map[common.Hash]common.Hash{}
	st[types.L1BaseFeeSlot] = common.BigToHash(fp.l1BaseFee)
	st[types.L1BlobBaseFeeSlot] = common.BigToHash(fp.blobBaseFee)
	var slot3 common.Hash
	binary.BigEndian.PutUint32(slot3[16:20], fp.baseFeeScalar)
	binary.BigEndian.PutUint32(slot3[20:24], fp.blobBaseFeeScalar)
	st[types.L1FeeScalarsSlot] = slot3
	var slot8 common.Hash
	if jovianCfg && !fp.isthmusLayout {
		binary.BigEndian.PutUint16(slot8[18:20], fp.daScalar)
	}
	binary.BigEndian.PutUint32(slot8[20:24], fp.opFeeScalar)
	binary.BigEndian.PutUint64(slot8[24:32], fp.opFeeConstant)
	if slot8 != (common.Hash{}) {
		st[types.OperatorFeeParamsSlot] = slot8
	}
	return st
}

func (fp feeParams) attributesTx(label string, jovianCfg bool) inputTx {
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
		Data: fp.attributesData(jovianCfg),
	}
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
	// Nonce 1 keeps both predeploys non-empty under EIP-158: the attributes
	// deposit value-0 CALL touches the code-less L1Block account, and an
	// empty-but-storage-bearing account would be touch-deleted at commit
	// ("unexpected storage wiping" in chain_makers). Real predeploys are
	// non-empty (they carry code); nonce 1 models that without giving
	// L1Block code, keeping "no tx writes L1Block slots" structurally true.
	pre := types.GenesisAlloc{
		l1BlockAddr:       {Balance: big.NewInt(0), Nonce: 1, Storage: fp.l1BlockStorage(jovianCfg)},
		messagePasserAddr: {Balance: big.NewInt(0), Nonce: 1},
	}
	return inputCase{
		Info:                  caseInfo{Hardfork: fork, Description: desc},
		Genesis:               knobs,
		Coinbase:              sequencerVault,
		ParentBeaconBlockRoot: common.HexToHash("0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"),
		Pre:                   pre,
		Transactions: []inputTx{
			fp.attributesTx(fork+"_"+name, jovianCfg),
		},
	}
}

func fund(c *inputCase, key byte, amount *big.Int) {
	c.Pre[addrOfKey(key)] = types.Account{Balance: amount}
}

// ---------------------------------------------------------------------
// The 18 cases (14 from the M-B3+M6 plan table + 3 corpus-augmentation
// cases, spec rev.3, + 1 defer-sweep case)
// ---------------------------------------------------------------------

type caseSpec struct {
	name  string
	forks []string
	build func(fork string) inputCase
}

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
}

// vectorName maps a spec x fork to the checked-in file base name (matches
// vectors/manifest.txt; note the two Jovian-only cases keep their plan names
// jovian_first_block / jovian_da_mix).
func vectorName(fork, name string) string {
	return fork + "_" + name
}

func emitCases(dir string) error {
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	count := 0
	for _, spec := range caseSpecs {
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
