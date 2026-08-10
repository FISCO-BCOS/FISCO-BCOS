// Command opt8n-ref is the M-B3+M6 block-level OP-Stack vector generator for
// bcos-evm-ref. It drives op-geth AS A LIBRARY (checkout pinned in
// _op_test_vectors.generator_commit) through the exact block-building +
// re-validation pipeline op-geth itself uses in its tests:
//
//	core.GenerateChainWithGenesis  (AddTx == real core.ApplyTransaction,
//	                                FinalizeAndAssemble == real header commitments)
//	core.NewBlockChain + InsertChain on an independent fresh DB
//	                               (== real Process + ValidateState self-check)
//
// It descends from the M-T tx-level generator
// (bcos-evm/test/opstack/t8n/generator/main.go): buildTx's three arms
// (deposit / eip1559 / setcode) and MakePreState-style pre handling are
// reused; processVector is replaced by processBlockVector; postState emission
// is the plan's decision-record-8 "candidate set, all accounts, all slots"
// semantics instead of a diff.
//
// Two subcommands:
//
//	opt8n-ref --write-cases <dir>
//	    deterministically (re)writes the 33 corpus case files (*.in.json).
//	    The corpus definitions live in this file (see caseDefs) so that the
//	    L1Block slot values and the L1-attributes calldata can never drift
//	    apart by hand-editing -- and processBlockVector re-asserts their
//	    consistency at startup anyway (iron rule: slot<->calldata).
//
//	opt8n-ref --input <case.in.json> --output <vector.json> --op-geth-commit <sha>
//	    generates one v3-block vector. The hardfork is taken from the case's
//	    _info.hardfork (ecotone|fjord|granite|holocene|isthmus|jovian); upgrade
//	    boundaries use the chainConfigSpec shape (see buildChainConfigSpec).
//
// This file is checked into the FISCO-BCOS repo as source of truth but is
// built from inside an op-geth checkout (see README.md):
//
//	cp -r test/opstack/t8n/generator <op-geth>/cmd/opt8n-ref
//	cd <op-geth> && go build ./cmd/opt8n-ref
package main

import (
	"bytes"
	"crypto/ecdsa"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"math/big"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/common/hexutil"
	"github.com/ethereum/go-ethereum/common/math"
	"github.com/ethereum/go-ethereum/consensus/beacon"
	"github.com/ethereum/go-ethereum/consensus/ethash"
	"github.com/ethereum/go-ethereum/consensus/misc/eip1559"
	"github.com/ethereum/go-ethereum/core"
	"github.com/ethereum/go-ethereum/core/rawdb"
	"github.com/ethereum/go-ethereum/core/state"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/crypto"
	"github.com/ethereum/go-ethereum/ethdb"
	"github.com/ethereum/go-ethereum/params"
	"github.com/ethereum/go-ethereum/rlp"
	"github.com/ethereum/go-ethereum/trie"
	"github.com/ethereum/go-ethereum/triedb"
	"github.com/holiman/uint256"
)

// schemaVersion is `_op_test_vectors.version` (v3-block, plan schema).
const schemaVersion = "3-block"

// Fixed OP-Stack / system addresses (op-geth params + rollup_cost.go).
var (
	l1BlockAddr       = types.L1BlockAddr // 0x4200...0015
	messagePasserAddr = params.OptimismL2ToL1MessagePasser
	sequencerVault    = common.HexToAddress("0x4200000000000000000000000000000000000011")
	baseFeeVault      = params.OptimismBaseFeeRecipient
	l1FeeVault        = params.OptimismL1FeeRecipient
	operatorFeeVault  = params.OptimismOperatorFeeRecipient
	beaconRootsAddr   = params.BeaconRootsAddress    // EIP-4788
	historyStorage    = params.HistoryStorageAddress // EIP-2935
)

const ringSize = 8191 // EIP-4788 HISTORY_BUFFER_LENGTH == EIP-2935 window

func main() {
	var (
		writeCases     = flag.String("write-cases", "", "write the 33 corpus case files into <dir> and exit")
		probeFields    = flag.String("probe-receipt-fields", "", "dev probe: run one <case.in.json> through the self-contained op-geth pipeline and dump every receipt's OP-Stack field emission (presence + hex value), then exit")
		inputPath      = flag.String("input", "", "input case JSON")
		outputPath     = flag.String("output", "", "output vector JSON")
		opGethCommit   = flag.String("op-geth-commit", "unknown", "full sha of the op-geth checkout, recorded into _op_test_vectors.generator_commit")
		probeWrap      = flag.Bool("probe-genesis-number", false, "dev probe: attempt Genesis.Number=8191 (ring-wrap feasibility) and report")
		probeSpec      = flag.Bool("probe-spec", false, "dev probe: build representative chainConfigSpec values through buildChainConfigSpec and print each activation timeline (verifies the Task-0 upgrade-boundary interface), then exit")
		goldenOutput   = flag.String("golden-output", "", "Task 2 (engine gate golden ritual): also emit blockHash/transactionsRoot/extraData/excessBlobGas/rawTransactions/encodedHeaderHex for this vector to this path (vectors/ itself is untouched)")
		chainOutputDir = flag.String("chain-output-dir", "", "Task 2 Step 2: generate the off-line 1->2 chained golden pair (GenerateChainWithGenesis n=2, InsertChain-validated) into this directory and exit")
	)
	flag.Parse()

	switch {
	case *probeWrap:
		probeGenesisNumber()
	case *probeSpec:
		if err := probeChainConfigSpec(); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	case *writeCases != "":
		if err := emitCases(*writeCases); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	case *probeFields != "":
		raw, err := os.ReadFile(*probeFields)
		if err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
		var in inputCase
		if err := json.Unmarshal(raw, &in); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: parsing %s: %v\n", *probeFields, err)
			os.Exit(1)
		}
		if err := probeReceiptFields(&in); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	case *chainOutputDir != "":
		if err := runChainPair(*chainOutputDir, *opGethCommit); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	case *inputPath != "" && *outputPath != "":
		if err := run(*inputPath, *outputPath, *opGethCommit, *goldenOutput); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	default:
		fmt.Fprintln(os.Stderr, "usage: opt8n-ref --write-cases <dir> | --probe-receipt-fields <case.in.json> | --probe-spec | --probe-genesis-number | --input <case.in.json> --output <vector.json> [--golden-output <golden.json>] [--op-geth-commit <sha>] | --chain-output-dir <dir> [--op-geth-commit <sha>]")
		os.Exit(2)
	}
}

// ---------------------------------------------------------------------
// Input schema (.in.json)
// ---------------------------------------------------------------------

type caseInfo struct {
	Hardfork    string `json:"hardfork"`
	Description string `json:"description"`
}

// genesisKnobs are the ONLY environment inputs (plan decision record 7):
// env is an emission derived from the generated block header, not an input.
type genesisKnobs struct {
	Timestamp          math.HexOrDecimal64   `json:"timestamp"`
	GasLimit           math.HexOrDecimal64   `json:"gasLimit"`
	BaseFee            *math.HexOrDecimal256 `json:"baseFee,omitempty"`
	EIP1559Denominator math.HexOrDecimal64   `json:"eip1559Denominator"`
	EIP1559Elasticity  math.HexOrDecimal64   `json:"eip1559Elasticity"`
	MinBaseFee         *math.HexOrDecimal64  `json:"minBaseFee,omitempty"` // Jovian only (mandatory there)
}

type inputCase struct {
	Info                  caseInfo                         `json:"_info"`
	Genesis               genesisKnobs                     `json:"genesis"`
	Coinbase              common.Address                   `json:"coinbase"`
	ParentBeaconBlockRoot common.Hash                      `json:"parentBeaconBlockRoot"`
	Pre                   types.GenesisAlloc               `json:"pre"`
	Transactions          []inputTx                        `json:"transactions"`
	ExtraCandidates       []common.Address                 `json:"extra_candidates,omitempty"`
	ExtraStorage          map[common.Address][]common.Hash `json:"extra_storage,omitempty"`
}

type inputDeposit struct {
	From       common.Address        `json:"from"`
	To         *common.Address       `json:"to"`
	Mint       *math.HexOrDecimal256 `json:"mint,omitempty"`
	Value      *math.HexOrDecimal256 `json:"value,omitempty"`
	Gas        math.HexOrDecimal64   `json:"gas"`
	IsSystemTx bool                  `json:"is_system_tx"`
	SourceHash common.Hash           `json:"source_hash"`
}

// inputAuthorization is one EIP-7702 tuple. Two mutually-exclusive forms:
//   - normal: authSecretKey present -> types.SignSetCode (valid signature);
//   - raw override (spec rev.2 (2), corpus-augmentation): the three raw*
//     fields present -> the tuple is emitted UNSIGNED with yParity/r/s taken
//     verbatim, and MUST be marked _op_signer_unrecoverable=true and be
//     structurally unrecoverable (same predicate as the replayer). All new
//     fields are omitempty (iron rule: --write-cases re-emits the old 25
//     case files; a missing field must not inject null).
type inputAuthorization struct {
	ChainID       *math.HexOrDecimal256 `json:"chainId,omitempty"`
	Address       common.Address        `json:"address"`
	Nonce         math.HexOrDecimal64   `json:"nonce"`
	AuthSecretKey hexutil.Bytes         `json:"authSecretKey,omitempty"`

	SignerUnrecoverable bool            `json:"_op_signer_unrecoverable,omitempty"`
	RawYParity          *hexutil.Uint64 `json:"rawYParity,omitempty"`
	RawR                *hexutil.Big    `json:"rawR,omitempty"`
	RawS                *hexutil.Big    `json:"rawS,omitempty"`
}

type inputTx struct {
	OpType           string               `json:"_op_type"`
	OpDeposit        *inputDeposit        `json:"_op_deposit,omitempty"`
	OpAuthorizations []inputAuthorization `json:"_op_authorization_list,omitempty"`

	Nonce                *math.HexOrDecimal64  `json:"nonce,omitempty"`
	To                   *common.Address       `json:"to,omitempty"`
	Value                *math.HexOrDecimal256 `json:"value,omitempty"`
	Gas                  *math.HexOrDecimal64  `json:"gas,omitempty"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas,omitempty"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas,omitempty"`
	Data                 hexutil.Bytes         `json:"data,omitempty"`
	// EIP-2930 access list, same shape as the output field (omitempty, iron
	// rule: absent on the old 25 cases, must not re-emit as null).
	AccessList []outputAccessTuple   `json:"accessList,omitempty"`
	ChainID    *math.HexOrDecimal256 `json:"chainId,omitempty"`
	SecretKey  *hexutil.Bytes        `json:"secretKey,omitempty"`
}

// ---------------------------------------------------------------------
// Output schema (v3-block)
// ---------------------------------------------------------------------

type outputEnv struct {
	CurrentCoinbase       string `json:"currentCoinbase"`
	CurrentNumber         string `json:"currentNumber"`
	CurrentTimestamp      string `json:"currentTimestamp"`
	CurrentGasLimit       string `json:"currentGasLimit"`
	CurrentBaseFee        string `json:"currentBaseFee"`
	CurrentRandom         string `json:"currentRandom"`
	ParentBeaconBlockRoot string `json:"parentBeaconBlockRoot"`
	ParentHash            string `json:"parentHash"`
}

type outputDepositTx struct {
	OpType    string        `json:"_op_type"`
	OpDeposit inputDeposit  `json:"_op_deposit"`
	Data      hexutil.Bytes `json:"data"`
}

// outputAccessTuple is one EIP-2930 access-list entry, replayer field
// contract: {address, storageKeys[]}. StorageKeys is always emitted (an
// empty tuple must serialize as [], never null).
type outputAccessTuple struct {
	Address     common.Address `json:"address"`
	StorageKeys []common.Hash  `json:"storageKeys"`
}

// math.HexOrDecimal256 marshals via pointer receiver only -- every such field
// below must stay a pointer (M-T gotcha, kept).
//
// AccessList / SignerUnrecoverable below deliberately deviate from the
// no-omitempty style of this schema: the old 25 vectors predate the fields
// and byte-invariance under regeneration (regen.sh's final diff clause)
// requires absent-not-null.
type outputSignedTx struct {
	OpType               string                `json:"_op_type"`
	OpRaw                string                `json:"_op_raw"`
	ChainID              *math.HexOrDecimal256 `json:"chainId"`
	Nonce                math.HexOrDecimal64   `json:"nonce"`
	To                   *common.Address       `json:"to"`
	Gas                  math.HexOrDecimal64   `json:"gas"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas"`
	Value                *math.HexOrDecimal256 `json:"value"`
	Data                 hexutil.Bytes         `json:"data"`
	AccessList           []outputAccessTuple   `json:"accessList,omitempty"`
	Sender               common.Address        `json:"sender"`
}

type outputAuthorization struct {
	ChainID *math.HexOrDecimal256 `json:"chainId"`
	Address common.Address        `json:"address"`
	Nonce   math.HexOrDecimal64   `json:"nonce"`
	YParity math.HexOrDecimal64   `json:"yParity"`
	R       *math.HexOrDecimal256 `json:"r"`
	S       *math.HexOrDecimal256 `json:"s"`
	// true only on raw-override tuples (see inputAuthorization); omitempty is
	// the marker contract -- the replayer treats field presence as marked.
	SignerUnrecoverable bool `json:"_op_signer_unrecoverable,omitempty"`
}

type outputSetCodeTx struct {
	OpType               string                `json:"_op_type"`
	OpRaw                string                `json:"_op_raw"`
	ChainID              *math.HexOrDecimal256 `json:"chainId"`
	Nonce                math.HexOrDecimal64   `json:"nonce"`
	To                   common.Address        `json:"to"`
	Gas                  math.HexOrDecimal64   `json:"gas"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas"`
	Value                *math.HexOrDecimal256 `json:"value"`
	Data                 hexutil.Bytes         `json:"data"`
	AccessList           []outputAccessTuple   `json:"accessList,omitempty"`
	Sender               common.Address        `json:"sender"`
	OpAuthorizationList  []outputAuthorization `json:"_op_authorization_list"`
}

type outputBlock struct {
	Transactions []json.RawMessage `json:"transactions"`
}

type expectedHeader struct {
	GasUsed         string `json:"gasUsed"`
	ReceiptsRoot    string `json:"receiptsRoot"`
	LogsBloom       string `json:"logsBloom"`
	WithdrawalsRoot string `json:"withdrawalsRoot"`
	RequestsHash    string `json:"requestsHash"`
	BlobGasUsed     string `json:"blobGasUsed"`
	StateRoot       string `json:"stateRoot"`
}

type expectedReceipt struct {
	Type                    string  `json:"type"`
	Status                  string  `json:"status"`
	GasUsed                 string  `json:"gasUsed"`
	CumulativeGasUsed       string  `json:"cumulativeGasUsed"`
	LogsCount               int     `json:"logsCount"`
	OpDepositNonce          *string `json:"_op_deposit_nonce,omitempty"`
	OpDepositReceiptVersion *string `json:"_op_deposit_receipt_version,omitempty"`
	OpL1Fee                 *string `json:"_op_l1_fee,omitempty"`
	OpOperatorFee           *string `json:"_op_operator_fee,omitempty"`
	OpDaFootprint           *string `json:"_op_da_footprint,omitempty"`
	// 线 C 新增（按 OP_RECEIPT_FIELDMAP.md 发射面）
	OpL1GasPrice           *string `json:"_op_l1_gas_price,omitempty"`
	OpL1BlobBaseFee        *string `json:"_op_l1_blob_base_fee,omitempty"`
	OpL1GasUsed            *string `json:"_op_l1_gas_used,omitempty"`
	OpL1BaseFeeScalar      *string `json:"_op_l1_base_fee_scalar,omitempty"`
	OpL1BlobBaseFeeScalar  *string `json:"_op_l1_blob_base_fee_scalar,omitempty"`
	OpOperatorFeeScalar    *string `json:"_op_operator_fee_scalar,omitempty"`
	OpOperatorFeeConstant  *string `json:"_op_operator_fee_constant,omitempty"`
	OpDaFootprintGasScalar *string `json:"_op_da_footprint_gas_scalar,omitempty"`
}

type opExpected struct {
	Header   expectedHeader    `json:"header"`
	Receipts []expectedReceipt `json:"receipts"`
}

// outputAccount is the canonical EF state-test account shape:
// balance/nonce/code ALWAYS present (storage only when non-empty). The C++
// replayer feeds `pre` verbatim to evmone's from_json<TestState>, which
// hard-requires all three fields (statetest_loader.cpp:353-356, j_acc.at).
// types.GenesisAlloc's own marshaling (omitempty on Nonce/Code) is a geth
// t8n-alloc convention, not the EF shape — emitting it for `pre` made every
// vector unparseable on the replay side. `postState` intentionally stays
// GenesisAlloc-shaped: its {"balance":"0x0"} zero-account convention (see
// emitPostState) is parsed by the replayer's own comparator, not by evmone.
type outputAccount struct {
	Balance *math.HexOrDecimal256       `json:"balance"`
	Nonce   math.HexOrDecimal64         `json:"nonce"`
	Code    hexutil.Bytes               `json:"code"`
	Storage map[common.Hash]common.Hash `json:"storage,omitempty"`
}

func emitPre(alloc types.GenesisAlloc) map[common.Address]outputAccount {
	out := make(map[common.Address]outputAccount, len(alloc))
	for addr, acc := range alloc {
		bal := acc.Balance
		if bal == nil {
			bal = new(big.Int)
		}
		oa := outputAccount{
			Balance: (*math.HexOrDecimal256)(bal),
			Nonce:   math.HexOrDecimal64(acc.Nonce),
			Code:    hexutil.Bytes(acc.Code), // nil -> "0x"
		}
		if len(acc.Storage) > 0 {
			oa.Storage = acc.Storage
		}
		out[addr] = oa
	}
	return out
}

type outputVector struct {
	Info       caseInfo                         `json:"_info"`
	Env        outputEnv                        `json:"env"`
	Pre        map[common.Address]outputAccount `json:"pre"`
	Block      outputBlock                      `json:"block"`
	PostState  types.GenesisAlloc               `json:"postState"`
	OpExpected opExpected                       `json:"_op_expected"`
}

// ---------------------------------------------------------------------
// Chain config (per-fork recipe, mirroring op-geth
// miner/payload_building_test.go holoceneConfig()/isthmusConfig()/
// jovianConfig() and params.OptimismTestConfig: start from
// OptimismTestConfig and nil-out later forks. OptimismTestConfig already has
// Regolith..Jovian at time 0, KarstTime=nil, OsakaTime=nil, InteropTime=nil,
// and the ETH twins Shanghai/Cancun/Prague at 0 -- which is exactly what
// CheckOptimismValidity requires (Shanghai==Canyon, Cancun==Ecotone,
// Prague==Isthmus). Every pre-Isthmus recipe therefore ALSO nils PragueTime
// together with IsthmusTime (ETH twin: equalPtrValues(Prague, Isthmus)).
// Note the consequence: nil PragueTime disables EIP-2935 / EIP-7702 /
// requestsHash, so ecotone/fjord/granite/holocene cases cannot use the
// setcode/7702 arm (and ecotoneConfig/fjordConfig run at EVMC_CANCUN).
// ---------------------------------------------------------------------

func buildChainConfig(fork string) (*params.ChainConfig, error) {
	conf := *params.OptimismTestConfig
	conf.ChainID = big.NewInt(8453) // 0x2105, Base mainnet, matches the plan's schema example
	switch fork {
	case "jovian":
		// OptimismTestConfig already sets JovianTime = 0 (Regolith..Jovian at 0).
	case "isthmus":
		conf.JovianTime = nil
	case "holocene":
		conf.IsthmusTime = nil
		conf.JovianTime = nil
		conf.PragueTime = nil // ETH twin: PragueTime == IsthmusTime
	case "granite":
		conf.HoloceneTime = nil
		conf.IsthmusTime = nil
		conf.JovianTime = nil
		conf.PragueTime = nil
	case "fjord":
		conf.GraniteTime = nil
		conf.HoloceneTime = nil
		conf.IsthmusTime = nil
		conf.JovianTime = nil
		conf.PragueTime = nil
	case "ecotone":
		conf.FjordTime = nil
		conf.GraniteTime = nil
		conf.HoloceneTime = nil
		conf.IsthmusTime = nil
		conf.JovianTime = nil
		conf.PragueTime = nil
	default:
		return nil, fmt.Errorf("unknown hardfork %q (want ecotone|fjord|granite|holocene|isthmus|jovian)", fork)
	}
	if err := conf.CheckOptimismValidity(); err != nil {
		return nil, fmt.Errorf("chain config invalid: %w", err)
	}
	return &conf, nil
}

// chainConfigSpec is the upgrade-boundary config shape: base = the fork the
// chain starts at genesis; activations = per-fork activation timestamps.
// buildChainConfig(fork) only produces fork-at-0 configs (a single block
// cannot express "genesis in old fork, one block crosses into a new fork");
// this shape does. The single block's timestamp is controlled by the case's
// blockTime (= genesisTime+10, chain_makers.makeHeader), so an activation T
// must satisfy genesisTime < T <= genesisTime+10 -- the case constructor
// picks genesisTime in [T-10, T-1]. Consumed by Tasks 1/3 for the
// upgrade-boundary vectors; here it is exercised by --probe-spec.
type chainConfigSpec struct {
	base        string            // ecotone|fjord|granite|holocene|isthmus|jovian
	activations map[string]uint64 // may be nil; e.g. {"fjord": T} => FjordTime=T
}

// buildChainConfigSpec starts from buildChainConfig(spec.base) and then sets
// the per-fork activation timestamps. ETH twins are re-coupled on activation
// (canyon->Shanghai, ecotone->Cancun, isthmus->Prague per CheckOptimismValidity)
// so a pre-Isthmus base activated into Isthmus stays valid.
func buildChainConfigSpec(spec chainConfigSpec) (*params.ChainConfig, error) {
	cfg, err := buildChainConfig(spec.base)
	if err != nil {
		return nil, err
	}
	for fork, ts := range spec.activations {
		switch fork {
		case "regolith":
			cfg.RegolithTime = uint64Ptr(ts)
		case "canyon":
			cfg.CanyonTime = uint64Ptr(ts)
			cfg.ShanghaiTime = uint64Ptr(ts) // ETH twin
		case "ecotone":
			cfg.EcotoneTime = uint64Ptr(ts)
			cfg.CancunTime = uint64Ptr(ts) // ETH twin
		case "fjord":
			cfg.FjordTime = uint64Ptr(ts)
		case "granite":
			cfg.GraniteTime = uint64Ptr(ts)
		case "holocene":
			cfg.HoloceneTime = uint64Ptr(ts)
		case "isthmus":
			cfg.IsthmusTime = uint64Ptr(ts)
			cfg.PragueTime = uint64Ptr(ts) // ETH twin
		case "jovian":
			cfg.JovianTime = uint64Ptr(ts)
		default:
			return nil, fmt.Errorf("unknown activation fork %q", fork)
		}
	}
	if err := cfg.CheckOptimismValidity(); err != nil {
		return nil, fmt.Errorf("chain config invalid: %w", err)
	}
	return cfg, nil
}

func uint64Ptr(v uint64) *uint64 { return &v }

// probeChainConfigSpec is a self-contained dev probe (--probe-spec) that
// builds representative chainConfigSpec values through buildChainConfigSpec
// and prints each fork activation timeline. It verifies the Task-0 acceptance
// criteria -- the interface can express pure Ecotone/Fjord AND upgrade
// boundaries, with the ETH twins nil'd/co-activated together -- without
// touching the golden path. The 10-second coupling is demonstrated by placing
// every activation at T=1005 with the corpus-default genesisTime=1000 /
// blockTime=1010 (1000 < 1005 <= 1010 holds).
func probeChainConfigSpec() error {
	specs := []struct {
		label string
		spec  chainConfigSpec
	}{
		{"pure ecotone", chainConfigSpec{base: "ecotone"}},
		{"pure fjord", chainConfigSpec{base: "fjord"}},
		{"pure granite", chainConfigSpec{base: "granite"}},
		{"pure holocene", chainConfigSpec{base: "holocene"}},
		{"pure isthmus", chainConfigSpec{base: "isthmus"}},
		{"pure jovian", chainConfigSpec{base: "jovian"}},
		{"upgrade ecotone->fjord @1005", chainConfigSpec{base: "ecotone", activations: map[string]uint64{"fjord": 1005}}},
		{"upgrade fjord->granite @1005", chainConfigSpec{base: "fjord", activations: map[string]uint64{"granite": 1005}}},
		{"upgrade granite->holocene @1005", chainConfigSpec{base: "granite", activations: map[string]uint64{"holocene": 1005}}},
		{"upgrade holocene->isthmus @1005", chainConfigSpec{base: "holocene", activations: map[string]uint64{"isthmus": 1005}}},
		{"upgrade isthmus->jovian @1005", chainConfigSpec{base: "isthmus", activations: map[string]uint64{"jovian": 1005}}},
	}
	for _, s := range specs {
		cfg, err := buildChainConfigSpec(s.spec)
		if err != nil {
			return fmt.Errorf("spec %q: %w", s.label, err)
		}
		fmt.Printf("%-30s chainID=%s\n", s.label, cfg.ChainID)
		fmt.Printf("  regolith=%-4s canyon=%-4s ecotone=%-4s fjord=%-4s granite=%-4s holocene=%-4s isthmus=%-4s jovian=%-4s\n",
			ptrOrNil(cfg.RegolithTime), ptrOrNil(cfg.CanyonTime), ptrOrNil(cfg.EcotoneTime), ptrOrNil(cfg.FjordTime),
			ptrOrNil(cfg.GraniteTime), ptrOrNil(cfg.HoloceneTime), ptrOrNil(cfg.IsthmusTime), ptrOrNil(cfg.JovianTime))
		fmt.Printf("  shanghai=%-4s cancun=%-4s prague=%-4s   (ETH twins: shanghai==canyon, cancun==ecotone, prague==isthmus)\n",
			ptrOrNil(cfg.ShanghaiTime), ptrOrNil(cfg.CancunTime), ptrOrNil(cfg.PragueTime))
	}
	return nil
}

func ptrOrNil(p *uint64) string {
	if p == nil {
		return "nil"
	}
	return fmt.Sprintf("%d", *p)
}

// ---------------------------------------------------------------------
// Vector generation
// ---------------------------------------------------------------------

func run(inputPath, outputPath, opGethCommit, goldenOutputPath string) error {
	raw, err := os.ReadFile(inputPath)
	if err != nil {
		return err
	}
	var in inputCase
	if err := json.Unmarshal(raw, &in); err != nil {
		return fmt.Errorf("parsing %s: %w", inputPath, err)
	}
	id := strings.TrimSuffix(filepath.Base(inputPath), ".in.json")

	vec, golden, err := processBlockVector(&in, id)
	if err != nil {
		return fmt.Errorf("vector %q: %w", id, err)
	}

	meta, err := json.Marshal(struct {
		Version         string `json:"version"`
		Generator       string `json:"generator"`
		GeneratorCommit string `json:"generator_commit"`
	}{schemaVersion, "opt8n-ref", opGethCommit})
	if err != nil {
		return err
	}
	out := map[string]json.RawMessage{"_op_test_vectors": meta, id: vec}
	outBytes, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	outBytes = append(outBytes, '\n')
	if err := os.WriteFile(outputPath, outBytes, 0o644); err != nil {
		return err
	}
	if goldenOutputPath == "" {
		return nil
	}
	return writeJSON(goldenOutputPath, golden)
}

func processBlockVector(in *inputCase, id string) (json.RawMessage, *goldenRecord, error) {
	fork := in.Info.Hardfork
	cfg, err := buildChainConfig(fork)
	if err != nil {
		return nil, nil, err
	}

	// --- Startup consistency assertion (iron rule 2): L1Block slots <->
	// attributes calldata, field for field, plus the first-Ecotone-fallback
	// trap guard. Mismatch = corpus authoring error, hard stop.
	if err := assertL1BlockConsistency(cfg, in); err != nil {
		return nil, nil, fmt.Errorf("L1Block slot<->calldata consistency: %w", err)
	}
	if err := assertDepositsFirst(in.Transactions); err != nil {
		return nil, nil, err
	}

	// --- Genesis (iron rule 1: extraData on BOTH genesis and the generated
	// block; 9B Isthmus / 17B Jovian incl. mandatory minBaseFee).
	genesisTime := uint64(in.Genesis.Timestamp)
	blockTime := genesisTime + 10 // chain_makers.makeHeader: block time fixed at parent+10
	denom := uint64(in.Genesis.EIP1559Denominator)
	elasticity := uint64(in.Genesis.EIP1559Elasticity)
	var minBaseFee *uint64
	if cfg.IsJovian(genesisTime) {
		if in.Genesis.MinBaseFee == nil {
			return nil, nil, fmt.Errorf("jovian case must set genesis.minBaseFee (EncodeOptimismExtraData requires it)")
		}
		v := uint64(*in.Genesis.MinBaseFee)
		minBaseFee = &v
	}
	var genesisBaseFee *big.Int
	if in.Genesis.BaseFee != nil {
		genesisBaseFee = (*big.Int)(in.Genesis.BaseFee)
	}
	genesis := &core.Genesis{
		Config:     cfg,
		Timestamp:  genesisTime,
		GasLimit:   uint64(in.Genesis.GasLimit),
		BaseFee:    genesisBaseFee,
		Difficulty: big.NewInt(0),
		ExtraData:  eip1559.EncodeOptimismExtraData(cfg, genesisTime, denom, elasticity, minBaseFee),
		Alloc:      in.Pre,
	}

	// --- Build transactions. Signer per iron rule 9: MakeSigner(cfg, 1, blockTime).
	signer := types.MakeSigner(cfg, big.NewInt(1), blockTime)
	var (
		txs    []*types.Transaction
		outTxs []json.RawMessage
	)
	for i := range in.Transactions {
		tx, outTx, err := buildTx(&in.Transactions[i], signer, cfg)
		if err != nil {
			return nil, nil, fmt.Errorf("tx %d: %w", i, err)
		}
		txs = append(txs, tx)
		outTxs = append(outTxs, outTx)
	}

	// --- Generate the block (iron rules 1(ii), 3, 4).
	blockExtra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)
	engine := beacon.New(ethash.NewFaker())
	var (
		db          ethdb.Database
		blocks      []*types.Block
		receiptsAll []types.Receipts
	)
	if err := func() (err error) {
		defer func() {
			if r := recover(); r != nil {
				err = fmt.Errorf("GenerateChainWithGenesis panic: %v", r)
			}
		}()
		db, blocks, receiptsAll = core.GenerateChainWithGenesis(genesis, engine, 1, func(i int, b *core.BlockGen) {
			b.SetCoinbase(in.Coinbase)
			b.SetExtra(blockExtra)                          // iron rule 1(ii): InsertChain verifyHeader rejects otherwise
			b.SetParentBeaconRoot(in.ParentBeaconBlockRoot) // iron rule 4: 4788 build/replay symmetry
			for _, tx := range txs {
				b.AddTx(tx) // L1CostFunc/OperatorCostFunc wired inside NewEVMBlockContext (core/evm.go)
			}
		})
		return nil
	}(); err != nil {
		return nil, nil, err
	}
	if len(blocks) != 1 {
		return nil, nil, fmt.Errorf("expected 1 generated block, got %d", len(blocks))
	}
	block, receipts := blocks[0], receiptsAll[0]
	header := block.Header()
	if header.Time != blockTime || header.Number.Uint64() != 1 {
		return nil, nil, fmt.Errorf("unexpected generated header number/time: %d/%d", header.Number.Uint64(), header.Time)
	}
	if len(receipts) != len(txs) {
		return nil, nil, fmt.Errorf("receipt/tx count mismatch: %d vs %d", len(receipts), len(txs))
	}

	// --- Self-check (iron rule 5): independent fresh DB, real
	// Process+ValidateState. Failure = generator/corpus defect; NEVER bypass.
	if err := selfCheck(genesis, blocks); err != nil {
		return nil, nil, fmt.Errorf("InsertChain self-check FAILED (corpus/generator defect, do not bypass): %w", err)
	}

	out, golden, err := assembleOutput(in, cfg, signer, db, block, receipts, txs, outTxs)
	if err != nil {
		return nil, nil, fmt.Errorf("vector %q: %w", id, err)
	}
	vec, err := json.Marshal(out)
	if err != nil {
		return nil, nil, err
	}
	return vec, golden, nil
}

// probeReceiptFields is a SELF-CONTAINED dev probe that drives the op-geth
// pipeline for one case and dumps the full OP-Stack receipt field emission
// surface (presence + hex value) of every receipt. It deliberately does NOT
// reuse processBlockVector: that path returns (json.RawMessage, *goldenRecord,
// error) and hides receipts, and refactoring its front half would disturb the
// golden path (selfCheck / assembleOutput need genesis/blocks/db/txs/outTxs/
// signer). A throwaway probe -- the front half is copied verbatim below
// (main.go:436-517, genesis construction -> tx signing ->
// GenerateChainWithGenesis -> receiptsAll); the copy produces
// signer/db/engine/genesis/txs/blocks/receiptsAll. Only receiptsAll is read.
// This feeds the OP_RECEIPT_FIELDMAP.md fieldmap (Phase 2 line C task 0).
func probeReceiptFields(in *inputCase) error {
	// cfg/fork -- replaces processBlockVector :431-434.
	cfg, err := buildChainConfig(in.Info.Hardfork)
	if err != nil {
		return err
	}

	// ── copied from processBlockVector front half (main.go:436-517) ──
	// --- Startup consistency assertion (iron rule 2): L1Block slots <->
	// attributes calldata, field for field, plus the first-Ecotone-fallback
	// trap guard. Mismatch = corpus authoring error, hard stop.
	if err := assertL1BlockConsistency(cfg, in); err != nil {
		return fmt.Errorf("L1Block slot<->calldata consistency: %w", err)
	}
	if err := assertDepositsFirst(in.Transactions); err != nil {
		return err
	}

	// --- Genesis (iron rule 1: extraData on BOTH genesis and the generated
	// block; 9B Isthmus / 17B Jovian incl. mandatory minBaseFee).
	genesisTime := uint64(in.Genesis.Timestamp)
	blockTime := genesisTime + 10 // chain_makers.makeHeader: block time fixed at parent+10
	denom := uint64(in.Genesis.EIP1559Denominator)
	elasticity := uint64(in.Genesis.EIP1559Elasticity)
	var minBaseFee *uint64
	if cfg.IsJovian(genesisTime) {
		if in.Genesis.MinBaseFee == nil {
			return fmt.Errorf("jovian case must set genesis.minBaseFee (EncodeOptimismExtraData requires it)")
		}
		v := uint64(*in.Genesis.MinBaseFee)
		minBaseFee = &v
	}
	var genesisBaseFee *big.Int
	if in.Genesis.BaseFee != nil {
		genesisBaseFee = (*big.Int)(in.Genesis.BaseFee)
	}
	genesis := &core.Genesis{
		Config:     cfg,
		Timestamp:  genesisTime,
		GasLimit:   uint64(in.Genesis.GasLimit),
		BaseFee:    genesisBaseFee,
		Difficulty: big.NewInt(0),
		ExtraData:  eip1559.EncodeOptimismExtraData(cfg, genesisTime, denom, elasticity, minBaseFee),
		Alloc:      in.Pre,
	}

	// --- Build transactions. Signer per iron rule 9: MakeSigner(cfg, 1, blockTime).
	signer := types.MakeSigner(cfg, big.NewInt(1), blockTime)
	var txs []*types.Transaction
	for i := range in.Transactions {
		tx, _, err := buildTx(&in.Transactions[i], signer, cfg)
		if err != nil {
			return fmt.Errorf("tx %d: %w", i, err)
		}
		txs = append(txs, tx)
	}

	// --- Generate the block (iron rules 1(ii), 3, 4).
	blockExtra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)
	engine := beacon.New(ethash.NewFaker())
	var (
		db          ethdb.Database
		blocks      []*types.Block
		receiptsAll []types.Receipts
	)
	if err := func() (err error) {
		defer func() {
			if r := recover(); r != nil {
				err = fmt.Errorf("GenerateChainWithGenesis panic: %v", r)
			}
		}()
		db, blocks, receiptsAll = core.GenerateChainWithGenesis(genesis, engine, 1, func(i int, b *core.BlockGen) {
			b.SetCoinbase(in.Coinbase)
			b.SetExtra(blockExtra)                          // iron rule 1(ii): InsertChain verifyHeader rejects otherwise
			b.SetParentBeaconRoot(in.ParentBeaconBlockRoot) // iron rule 4: 4788 build/replay symmetry
			for _, tx := range txs {
				b.AddTx(tx) // L1CostFunc/OperatorCostFunc wired inside NewEVMBlockContext (core/evm.go)
			}
		})
		return nil
	}(); err != nil {
		return err
	}
	if len(blocks) != 1 {
		return fmt.Errorf("expected 1 generated block, got %d", len(blocks))
	}
	// ── end copied front half (skips :518 `block, receipts := blocks[0], receiptsAll[0]`) ──
	// db is only read by assembleOutput on the golden path; unused here.
	_ = db

	receipts := receiptsAll[0]
	for i, r := range receipts {
		fmt.Printf("tx[%d] type=0x%02x\n", i, r.Type)
		if r.DepositNonce != nil {
			fmt.Printf("  DepositNonce\t0x%x\n", *r.DepositNonce)
		} else {
			fmt.Printf("  DepositNonce\tabsent\n")
		}
		if r.DepositReceiptVersion != nil {
			fmt.Printf("  DepositReceiptVersion\t0x%x\n", *r.DepositReceiptVersion)
		} else {
			fmt.Printf("  DepositReceiptVersion\tabsent\n")
		}
		if r.L1GasPrice != nil {
			fmt.Printf("  L1GasPrice\t0x%x\n", r.L1GasPrice)
		} else {
			fmt.Printf("  L1GasPrice\tabsent\n")
		}
		if r.L1BlobBaseFee != nil {
			fmt.Printf("  L1BlobBaseFee\t0x%x\n", r.L1BlobBaseFee)
		} else {
			fmt.Printf("  L1BlobBaseFee\tabsent\n")
		}
		if r.L1GasUsed != nil {
			fmt.Printf("  L1GasUsed\t0x%x\n", r.L1GasUsed)
		} else {
			fmt.Printf("  L1GasUsed\tabsent\n")
		}
		if r.L1Fee != nil {
			fmt.Printf("  L1Fee\t0x%x\n", r.L1Fee)
		} else {
			fmt.Printf("  L1Fee\tabsent\n")
		}
		if r.FeeScalar != nil {
			fmt.Printf("  FeeScalar\t%s\n", r.FeeScalar.String())
		} else {
			fmt.Printf("  FeeScalar\tabsent\n")
		}
		if r.L1BaseFeeScalar != nil {
			fmt.Printf("  L1BaseFeeScalar\t0x%x\n", *r.L1BaseFeeScalar)
		} else {
			fmt.Printf("  L1BaseFeeScalar\tabsent\n")
		}
		if r.L1BlobBaseFeeScalar != nil {
			fmt.Printf("  L1BlobBaseFeeScalar\t0x%x\n", *r.L1BlobBaseFeeScalar)
		} else {
			fmt.Printf("  L1BlobBaseFeeScalar\tabsent\n")
		}
		if r.OperatorFeeScalar != nil {
			fmt.Printf("  OperatorFeeScalar\t0x%x\n", *r.OperatorFeeScalar)
		} else {
			fmt.Printf("  OperatorFeeScalar\tabsent\n")
		}
		if r.OperatorFeeConstant != nil {
			fmt.Printf("  OperatorFeeConstant\t0x%x\n", *r.OperatorFeeConstant)
		} else {
			fmt.Printf("  OperatorFeeConstant\tabsent\n")
		}
		if r.DAFootprintGasScalar != nil {
			fmt.Printf("  DAFootprintGasScalar\t0x%x\n", *r.DAFootprintGasScalar)
		} else {
			fmt.Printf("  DAFootprintGasScalar\tabsent\n")
		}
		if r.BlobGasUsed != 0 {
			fmt.Printf("  BlobGasUsed\t0x%x\n", r.BlobGasUsed)
		} else {
			fmt.Printf("  BlobGasUsed\tabsent\n")
		}
	}
	return nil
}

// assembleOutput turns one generated block (still open on its generation db,
// together with its receipts/txs) into both the existing v3-block
// outputVector and the Task 2 engine-gate golden record (spec §7.1:
// blockHash/transactionsRoot/extraData/excessBlobGas/rawTransactions/
// encodedHeaderHex -- extraData taken VERBATIM from header.Extra, no
// hand-picked value). This is the "扩展 opt8n-ref 发射段" (裁定 A3): the
// candidate-set / postState / receipt-expectation logic below is byte-for-
// byte what processBlockVector always computed (moved here unchanged, just
// with the pre-generation and post-generation halves of the candidate-set
// build merged into one pass now that txs/receipts are both in scope) --
// only the golden extraction at the end is new. Shared verbatim between the
// 33-case single-block path (processBlockVector) and the off-line 1->2
// chained pair (processChainPair, Step 2): both need identical assembly
// logic, just fed a different (in, db, block, receipts, txs, signer) tuple
// per block.
func assembleOutput(in *inputCase, cfg *params.ChainConfig, signer types.Signer, db ethdb.Database, block *types.Block, receipts types.Receipts, txs []*types.Transaction, outTxs []json.RawMessage) (outputVector, *goldenRecord, error) {
	header := block.Header()

	// --- postState: decision-record-8 candidate set, all accounts, all slots,
	// with a trie-iteration completeness check (any post-state account or
	// storage slot outside the declared candidate sets = hard error).
	candidates := newAddrSet()
	for i := range txs {
		// Tx participants -> candidate set (senders, to, 7702 authorities).
		from, err := types.Sender(signer, txs[i])
		if err == nil {
			candidates.add(from)
		} else if txs[i].IsDepositTx() {
			candidates.add(in.Transactions[i].OpDeposit.From)
		} else {
			return outputVector{}, nil, fmt.Errorf("tx %d: sender recovery: %w", i, err)
		}
		if to := txs[i].To(); to != nil {
			candidates.add(*to)
		}
		for _, auth := range txs[i].SetCodeAuthorizations() {
			if authority, err := auth.Authority(); err == nil {
				candidates.add(authority)
			}
		}
	}
	// Created-contract candidates (tx/receipt paired by index): the runtime
	// half of the double-bottoming -- the corpus also pre-derives the same
	// addresses into extra_candidates, and the slot-completeness check would
	// expose any disagreement between the two.
	for i := range txs {
		if txs[i].To() == nil && receipts[i].ContractAddress != (common.Address{}) {
			candidates.add(receipts[i].ContractAddress)
		}
	}
	candidates.add(in.Coinbase)
	for _, a := range []common.Address{
		sequencerVault, baseFeeVault, l1FeeVault, operatorFeeVault,
		l1BlockAddr, messagePasserAddr, beaconRootsAddr, historyStorage,
	} {
		candidates.add(a)
	}
	for addr := range in.Pre {
		candidates.add(addr)
	}
	for _, addr := range in.ExtraCandidates {
		candidates.add(addr)
	}
	slotCands := buildSlotCandidates(in, header.Time, header.Number.Uint64())
	postState, postDB, err := emitPostState(db, block.Root(), candidates, slotCands)
	if err != nil {
		return outputVector{}, nil, fmt.Errorf("postState emission: %w", err)
	}

	// --- Receipt expectations. Operator fee is computed BY THIS GENERATOR
	// per fork formula (iron rule 7; rollup_cost.go:254-287) from the
	// pre-state slot-8 params, then cross-checked against op-geth's own
	// NewOperatorCostFunc and against the OperatorFeeVault balance delta.
	expReceipts, err := buildExpectedReceipts(cfg, in, txs, receipts, header.Time)
	if err != nil {
		return outputVector{}, nil, err
	}
	if err := crossCheckVaults(in, expReceipts, postDB); err != nil {
		return outputVector{}, nil, err
	}

	// --- Header expectations + env (both emissions from the header; iron rule 6).
	if header.WithdrawalsHash == nil || header.RequestsHash == nil || header.BlobGasUsed == nil {
		return outputVector{}, nil, fmt.Errorf("generated header missing WithdrawalsHash/RequestsHash/BlobGasUsed")
	}
	if header.MixDigest != (common.Hash{}) {
		return outputVector{}, nil, fmt.Errorf("generated header MixDigest expected zero, got %s", header.MixDigest)
	}
	out := outputVector{
		Info: in.Info,
		Env: outputEnv{
			CurrentCoinbase:       strings.ToLower(header.Coinbase.Hex()),
			CurrentNumber:         hexutil.EncodeUint64(header.Number.Uint64()),
			CurrentTimestamp:      hexutil.EncodeUint64(header.Time),
			CurrentGasLimit:       hexutil.EncodeUint64(header.GasLimit),
			CurrentBaseFee:        hexutil.EncodeBig(header.BaseFee),
			CurrentRandom:         "0x0", // header.MixDigest is zero (asserted above)
			ParentBeaconBlockRoot: header.ParentBeaconRoot.Hex(),
			ParentHash:            header.ParentHash.Hex(),
		},
		Pre:       emitPre(in.Pre),
		Block:     outputBlock{Transactions: outTxs},
		PostState: postState,
		OpExpected: opExpected{
			Header: expectedHeader{
				GasUsed:         hexutil.EncodeUint64(header.GasUsed),
				ReceiptsRoot:    header.ReceiptHash.Hex(),
				LogsBloom:       hexutil.Encode(header.Bloom[:]),
				WithdrawalsRoot: header.WithdrawalsHash.Hex(),
				RequestsHash:    header.RequestsHash.Hex(),
				BlobGasUsed:     hexutil.EncodeUint64(*header.BlobGasUsed),
				StateRoot:       header.Root.Hex(),
			},
			Receipts: expReceipts,
		},
	}

	golden, err := buildGoldenRecord(block, txs)
	if err != nil {
		return outputVector{}, nil, fmt.Errorf("golden record: %w", err)
	}
	return out, golden, nil
}

// goldenRecord is the Task 2 engine-gate golden extension (spec §7.1): the
// fields the corpus's own v3-block vectors are structurally missing, and
// nothing else -- vectors/*.json stay byte-for-byte unmodified.
//
//   - BlockHash/TransactionsRoot: block.Hash() / header.TxHash, the off-line
//     op-geth-backed values the newPayload gate cross-checks against
//     (diagnostic honesty per spec §7.1: NOT self-consistency).
//   - ExtraData: header.Extra VERBATIM (9B Isthmus / 17B Jovian incl. the
//     mandatory minBaseFee) -- rev.2's hand-picked-value scheme is retired.
//   - ExcessBlobGas: always "0x0" on this no-blob OP chain (asserted, not
//     assumed).
//   - RawTransactions: tx.MarshalBinary() for every tx INCLUDING the 0x7E
//     deposit envelope (outputDepositTx carries no raw bytes) -- doubles as
//     the Task 3 OpDepositEncode byte-level golden.
//   - EncodedHeaderHex: the full header RLP hex, for the field-level
//     encode()==golden assertion that must precede the hash() assertion
//     (spec §7.5, decision C3).
type goldenRecord struct {
	BlockHash        string   `json:"blockHash"`
	TransactionsRoot string   `json:"transactionsRoot"`
	ExtraData        string   `json:"extraData"`
	ExcessBlobGas    string   `json:"excessBlobGas"`
	RawTransactions  []string `json:"rawTransactions"`
	EncodedHeaderHex string   `json:"encodedHeaderHex"`
}

func buildGoldenRecord(block *types.Block, txs []*types.Transaction) (*goldenRecord, error) {
	header := block.Header()
	if header.ExcessBlobGas == nil || *header.ExcessBlobGas != 0 {
		return nil, fmt.Errorf("expected header.ExcessBlobGas == 0 (no-blob OP chain), got %v", header.ExcessBlobGas)
	}
	rawTxs := make([]string, len(txs))
	for i, tx := range txs {
		b, err := tx.MarshalBinary()
		if err != nil {
			return nil, fmt.Errorf("tx %d MarshalBinary: %w", i, err)
		}
		rawTxs[i] = hexutil.Encode(b)
	}
	headerRLP, err := rlp.EncodeToBytes(header)
	if err != nil {
		return nil, fmt.Errorf("header RLP encode: %w", err)
	}
	return &goldenRecord{
		BlockHash:        block.Hash().Hex(),
		TransactionsRoot: header.TxHash.Hex(),
		ExtraData:        hexutil.Encode(header.Extra),
		ExcessBlobGas:    hexutil.EncodeUint64(*header.ExcessBlobGas),
		RawTransactions:  rawTxs,
		EncodedHeaderHex: hexutil.Encode(headerRLP),
	}, nil
}

func writeJSON(path string, v any) error {
	b, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	return os.WriteFile(path, b, 0o644)
}

// ---------------------------------------------------------------------
// Off-line chained pair (Task 2 Step 2, spec §7.1 rev.3 decision A2): a
// dedicated 1->2 block chain generated in ONE GenerateChainWithGenesis(n=2)
// call (real chain_makers parent-chaining -- block B's pre-state IS block
// A's generated post-state) and re-validated by InsertChain-ing BOTH blocks
// together. This is NOT two independent single-block vectors spliced by hand
// (rev.2's "set A's blockHash as B's parentHash" splice is retired -- three
// ways broken: static validation would reject it first, the golden values
// would be wrong, and state/number would disagree with a real chain).
// ---------------------------------------------------------------------

func processChainPair(fork string) (outputVector, outputVector, *goldenRecord, *goldenRecord, error) {
	cfg, err := buildChainConfig(fork)
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, err
	}
	jovianCfg := fork == "jovian"
	fp := defaultFeeParams()
	if jovianCfg {
		fp.daScalar = 400 // non-zero DA scalar: block1 DA footprint (header.BlobGasUsed) > gasUsed, max branch observable
	}
	const (
		genesisTime = uint64(1000)
		denom       = uint64(50)
		elasticity  = uint64(6)
		gasLimit    = uint64(10_000_000)
	)
	beaconRoot := common.HexToHash("0x0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c")
	senderAddr := addrOfKey(1)
	genesisPre := types.GenesisAlloc{
		l1BlockAddr:       {Balance: big.NewInt(0), Nonce: 1, Storage: fp.l1BlockStorage(jovianCfg)},
		messagePasserAddr: {Balance: big.NewInt(0), Nonce: 1},
		senderAddr:        {Balance: eth(100)},
	}
	if jovianCfg {
		genesisPre[addrOfKey(2)] = types.Account{Balance: eth(100)} // block1 junk-calldata tx sender
	}
	// Jovian requires a non-nil minBaseFee in EncodeOptimismExtraData
	// (eip1559_optimism.go:51-53 panics otherwise) -- mandatory for the three
	// extraData encodings below (genesis + both blocks).
	var minBaseFee *uint64
	knobs := genesisKnobs{
		Timestamp:          math.HexOrDecimal64(genesisTime),
		GasLimit:           math.HexOrDecimal64(gasLimit),
		BaseFee:            hdu(1_000_000_000),
		EIP1559Denominator: math.HexOrDecimal64(denom),
		EIP1559Elasticity:  math.HexOrDecimal64(elasticity),
	}
	if jovianCfg {
		v := uint64(0)
		minBaseFee = &v
		knobs.MinBaseFee = hd64(0)
	}
	genesis := &core.Genesis{
		Config:     cfg,
		Timestamp:  genesisTime,
		GasLimit:   gasLimit,
		BaseFee:    big.NewInt(1_000_000_000),
		Difficulty: big.NewInt(0),
		ExtraData:  eip1559.EncodeOptimismExtraData(cfg, genesisTime, denom, elasticity, minBaseFee),
		Alloc:      genesisPre,
	}

	var (
		ins     [2]*inputCase
		txSets  [2][]*types.Transaction
		outSets [2][]json.RawMessage
	)
	engine := beacon.New(ethash.NewFaker())
	var (
		db          ethdb.Database
		blocks      []*types.Block
		receiptsAll []types.Receipts
	)
	if err := func() (err error) {
		defer func() {
			if r := recover(); r != nil {
				err = fmt.Errorf("GenerateChainWithGenesis panic: %v", r)
			}
		}()
		db, blocks, receiptsAll = core.GenerateChainWithGenesis(genesis, engine, 2, func(i int, bg *core.BlockGen) {
			blockTime := bg.Timestamp() // chain_makers: parent.Time()+10, real per-block advance
			blockExtra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)
			bg.SetCoinbase(sequencerVault)
			bg.SetExtra(blockExtra)
			bg.SetParentBeaconRoot(beaconRoot)
			signer := bg.Signer() // == MakeSigner(cfg, bg.Number(), bg.Timestamp())

			attr := fp.attributesTx(fmt.Sprintf("chain_%d", i), jovianCfg)
			tx0, outTx0, terr := buildTx(&attr, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("block %d attributes tx: %w", i, terr))
			}
			bg.AddTx(tx0)

			nonce := bg.TxNonce(senderAddr) // real chained nonce (0 in A, 1 in B), not hand-tracked
			transfer := transferTx(1, nonce, recA, eth(1), 21_000, nil)
			tx1, outTx1, terr := buildTx(&transfer, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("block %d transfer tx: %w", i, terr))
			}
			bg.AddTx(tx1)

			txs := []*types.Transaction{tx0, tx1}
			outTxs := []json.RawMessage{outTx0, outTx1}
			transactions := []inputTx{attr, transfer}
			// Jovian block1 carries a large-calldata tx so its DA footprint
			// (header.BlobGasUsed, daScalar=400) exceeds gasUsed -- the max
			// branch of the DA-footprint model is observable (and stays well
			// below gasLimit). Isthmus has no DA footprint; its chainA/B stay
			// byte-identical to the pre-W5 output.
			if jovianCfg && i == 0 {
				junk := transferTx(2, bg.TxNonce(addrOfKey(2)), recB, big.NewInt(0), 400_000, junkData("chain jovian block1", 5000))
				txJ, outTxJ, jerr := buildTx(&junk, signer, cfg)
				if jerr != nil {
					panic(fmt.Errorf("block %d junk calldata tx: %w", i, jerr))
				}
				bg.AddTx(txJ)
				txs = append(txs, txJ)
				outTxs = append(outTxs, outTxJ)
				transactions = append(transactions, junk)
			}

			in := &inputCase{
				Info:                  caseInfo{Hardfork: fork, Description: fmt.Sprintf("off-line chained pair (spec §7.1 Step 2, decision A2), block %d/2", i+1)},
				Genesis:               knobs,
				Coinbase:              sequencerVault,
				ParentBeaconBlockRoot: beaconRoot,
				Transactions:          transactions,
			}
			if i == 0 {
				in.Pre = genesisPre // block B's Pre is filled in AFTER generation, from A's postState
			}
			ins[i] = in
			txSets[i] = txs
			outSets[i] = outTxs
		})
		return nil
	}(); err != nil {
		return outputVector{}, outputVector{}, nil, nil, err
	}
	if len(blocks) != 2 {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("expected 2 generated blocks, got %d", len(blocks))
	}

	if err := assertL1BlockConsistency(cfg, ins[0]); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("block A: %w", err)
	}
	if err := assertDepositsFirst(ins[0].Transactions); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("block A: %w", err)
	}

	// --- Self-check (iron rule 5, extended to a real 2-block InsertChain):
	// independent fresh DB, Process+ValidateState over BOTH blocks in
	// sequence -- this IS the "经 InsertChain 头校验" the chain pair exists
	// to exercise (real parent-child header linkage), not two independently
	// self-checked singles spliced by hand.
	if err := selfCheck(genesis, blocks); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("chain pair InsertChain self-check FAILED: %w", err)
	}

	signerA := types.MakeSigner(cfg, blocks[0].Number(), blocks[0].Time())
	outA, goldenA, err := assembleOutput(ins[0], cfg, signerA, db, blocks[0], receiptsAll[0], txSets[0], outSets[0])
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("chain block A: %w", err)
	}

	// Block B's pre IS block A's post -- not a second seed (decision A2).
	ins[1].Pre = outA.PostState
	if err := assertL1BlockConsistency(cfg, ins[1]); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("block B: %w", err)
	}
	if err := assertDepositsFirst(ins[1].Transactions); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("block B: %w", err)
	}
	signerB := types.MakeSigner(cfg, blocks[1].Number(), blocks[1].Time())
	outB, goldenB, err := assembleOutput(ins[1], cfg, signerB, db, blocks[1], receiptsAll[1], txSets[1], outSets[1])
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("chain block B: %w", err)
	}

	return outA, outB, goldenA, goldenB, nil
}

// chainedBlockOutput is one block of the off-line chained pair (Task 2 Step
// 2): the full v3-block payload (same fields as outputVector) merged flatly
// with the golden extension (same fields as goldenRecord) -- there is no
// pre-existing vectors/chainX.json to complement, so unlike the 33 flat
// golden files, these carry the complete per-block record.
type chainedBlockOutput struct {
	Info       caseInfo                         `json:"_info"`
	Env        outputEnv                        `json:"env"`
	Pre        map[common.Address]outputAccount `json:"pre"`
	Block      outputBlock                      `json:"block"`
	PostState  types.GenesisAlloc               `json:"postState"`
	OpExpected opExpected                       `json:"_op_expected"`

	BlockHash        string   `json:"blockHash"`
	TransactionsRoot string   `json:"transactionsRoot"`
	ExtraData        string   `json:"extraData"`
	ExcessBlobGas    string   `json:"excessBlobGas"`
	RawTransactions  []string `json:"rawTransactions"`
	EncodedHeaderHex string   `json:"encodedHeaderHex"`
}

func runChainPair(outputDir, opGethCommit string) error {
	_ = opGethCommit // recorded in golden/engine/README.md's generation record, not per-file (chained pair has no vectors/ counterpart to key off)
	if err := os.MkdirAll(outputDir, 0o755); err != nil {
		return err
	}
	write := func(id string, out outputVector, g *goldenRecord) error {
		merged := chainedBlockOutput{
			Info: out.Info, Env: out.Env, Pre: out.Pre, Block: out.Block,
			PostState: out.PostState, OpExpected: out.OpExpected,
			BlockHash: g.BlockHash, TransactionsRoot: g.TransactionsRoot,
			ExtraData: g.ExtraData, ExcessBlobGas: g.ExcessBlobGas,
			RawTransactions: g.RawTransactions, EncodedHeaderHex: g.EncodedHeaderHex,
		}
		if err := writeJSON(filepath.Join(outputDir, id+".golden.json"), merged); err != nil {
			return err
		}
		if err := writeJSON(filepath.Join(outputDir, id+".pre.json"), out.Pre); err != nil {
			return err
		}
		if err := writeJSON(filepath.Join(outputDir, id+".post.json"), out.PostState); err != nil {
			return err
		}
		return nil
	}
	// Parameterized by fork (W5 review A#4/B#2): each fork produces its own
	// pair prefix so the jovian pair (jovianChainA/B) does not clobber the
	// existing isthmus chainA/B files.
	for _, p := range []struct{ fork, prefix string }{
		{"isthmus", "chain"},
		{"jovian", "jovianChain"},
	} {
		outA, outB, goldenA, goldenB, err := processChainPair(p.fork)
		if err != nil {
			return fmt.Errorf("%s chain pair: %w", p.fork, err)
		}
		if err := write(p.prefix+"A", outA, goldenA); err != nil {
			return fmt.Errorf("%sA: %w", p.prefix, err)
		}
		if err := write(p.prefix+"B", outB, goldenB); err != nil {
			return fmt.Errorf("%sB: %w", p.prefix, err)
		}
	}
	return nil
}

func selfCheck(genesis *core.Genesis, blocks []*types.Block) error {
	engine := beacon.New(ethash.NewFaker())
	chain, err := core.NewBlockChain(rawdb.NewMemoryDatabase(), genesis, engine, nil)
	if err != nil {
		return fmt.Errorf("NewBlockChain: %w", err)
	}
	defer chain.Stop()
	if _, err := chain.InsertChain(blocks); err != nil {
		return err
	}
	if got, want := chain.CurrentBlock().Hash(), blocks[len(blocks)-1].Hash(); got != want {
		return fmt.Errorf("head after InsertChain %s != generated %s", got, want)
	}
	return nil
}

// ---------------------------------------------------------------------
// L1Block slot <-> calldata consistency (iron rule 2)
//
// Calldata layout (rollup_cost.go extractL1GasParamsPostIsthmus /
// ExtractDAFootprintGasScalar):
//   [0:4]   selector (Isthmus 0x098999be / Jovian 0x3db6be2b)
//   [4:8]   baseFeeScalar        <-> slot 3 bytes [16:20]
//   [8:12]  blobBaseFeeScalar    <-> slot 3 bytes [20:24]
//   [36:68] l1BaseFee            <-> slot 1
//   [68:100] l1BlobBaseFee       <-> slot 7
//   [164:168] operatorFeeScalar  <-> slot 8 bytes [20:24]
//   [168:176] operatorFeeConstant<-> slot 8 bytes [24:32]
//   [176:178] daFootprintGasScalar (Jovian) <-> slot 8 bytes [18:20]
//             (slot mapping mirrors bcos-evm-ref OpFeeParams.h)
// ---------------------------------------------------------------------

func assertL1BlockConsistency(cfg *params.ChainConfig, in *inputCase) error {
	if len(in.Transactions) == 0 {
		return fmt.Errorf("case has no transactions (needs the L1 attributes deposit)")
	}
	tx0 := &in.Transactions[0]
	if tx0.OpType != "deposit" || tx0.OpDeposit == nil {
		return fmt.Errorf("tx 0 must be the L1 attributes deposit")
	}
	if tx0.OpDeposit.To == nil || *tx0.OpDeposit.To != l1BlockAddr {
		return fmt.Errorf("tx 0 must target the L1Block predeploy %s", l1BlockAddr)
	}
	data := []byte(tx0.Data)
	slot := func(n int64) common.Hash {
		acc, ok := in.Pre[l1BlockAddr]
		if !ok {
			return common.Hash{}
		}
		return acc.Storage[common.BigToHash(big.NewInt(n))]
	}
	slot1, slot3, slot7, slot8 := slot(1), slot(3), slot(7), slot(8)

	jovianCfg := cfg.IsJovian(uint64(in.Genesis.Timestamp))
	checkCommon := func() error {
		if !bytes.Equal(slot1[:], data[36:68]) {
			return fmt.Errorf("slot1 (l1BaseFee) %x != calldata[36:68] %x", slot1, data[36:68])
		}
		if !bytes.Equal(slot7[:], data[68:100]) {
			return fmt.Errorf("slot7 (blobBaseFee) %x != calldata[68:100] %x", slot7, data[68:100])
		}
		if !bytes.Equal(slot3[16:20], data[4:8]) {
			return fmt.Errorf("slot3[16:20] (baseFeeScalar) %x != calldata[4:8] %x", slot3[16:20], data[4:8])
		}
		if !bytes.Equal(slot3[20:24], data[8:12]) {
			return fmt.Errorf("slot3[20:24] (blobBaseFeeScalar) %x != calldata[8:12] %x", slot3[20:24], data[8:12])
		}
		if !bytes.Equal(slot8[20:24], data[164:168]) {
			return fmt.Errorf("slot8[20:24] (operatorFeeScalar) %x != calldata[164:168] %x", slot8[20:24], data[164:168])
		}
		if !bytes.Equal(slot8[24:32], data[168:176]) {
			return fmt.Errorf("slot8[24:32] (operatorFeeConstant) %x != calldata[168:176] %x", slot8[24:32], data[168:176])
		}
		// First-Ecotone fallback trap (rollup_cost.go:169-179): blob slot and
		// BOTH 4-byte scalars all zero would silently select the Bedrock cost
		// function. Forbidden corpus-wide.
		if slot7 == (common.Hash{}) && isZero(slot3[16:24]) {
			return fmt.Errorf("first-Ecotone fallback trap: slot7 and both slot3 scalars are all zero")
		}
		return nil
	}

	switch {
	case jovianCfg && len(data) == types.IsthmusL1AttributesLen:
		// Jovian config + Isthmus-length calldata = "first Jovian block"
		// form (case 12): deposits only, slot-8 DA bytes zero, skip [176:178].
		if !bytes.Equal(data[0:4], types.IsthmusL1AttributesSelector) {
			return fmt.Errorf("Isthmus-length attributes under Jovian must use the Isthmus selector, got %x", data[0:4])
		}
		for i := range in.Transactions {
			if in.Transactions[i].OpType != "deposit" {
				return fmt.Errorf("Jovian activation-form block must be deposits-only (tx %d is %q)", i, in.Transactions[i].OpType)
			}
		}
		if !isZero(slot8[18:20]) {
			return fmt.Errorf("Jovian activation-form block requires slot8 DA bytes [18:20] zero, got %x", slot8[18:20])
		}
		return checkCommon()
	case jovianCfg:
		if len(data) != types.JovianL1AttributesLen {
			return fmt.Errorf("Jovian attributes must be %d bytes (or %d for the activation form), got %d",
				types.JovianL1AttributesLen, types.IsthmusL1AttributesLen, len(data))
		}
		if !bytes.Equal(data[0:4], types.JovianL1AttributesSelector) {
			return fmt.Errorf("Jovian attributes selector mismatch: got %x", data[0:4])
		}
		if err := checkCommon(); err != nil {
			return err
		}
		if !bytes.Equal(slot8[18:20], data[176:178]) {
			return fmt.Errorf("slot8[18:20] (daFootprintGasScalar) %x != calldata[176:178] %x", slot8[18:20], data[176:178])
		}
		return nil
	default: // isthmus
		if len(data) != types.IsthmusL1AttributesLen {
			return fmt.Errorf("Isthmus attributes must be %d bytes, got %d", types.IsthmusL1AttributesLen, len(data))
		}
		if !bytes.Equal(data[0:4], types.IsthmusL1AttributesSelector) {
			return fmt.Errorf("Isthmus attributes selector mismatch: got %x", data[0:4])
		}
		return checkCommon()
	}
}

func assertDepositsFirst(txs []inputTx) error {
	seenNonDeposit := false
	for i := range txs {
		if txs[i].OpType == "deposit" {
			if seenNonDeposit {
				return fmt.Errorf("tx %d: deposit after non-deposit (deposits must come first)", i)
			}
		} else {
			seenNonDeposit = true
		}
	}
	return nil
}

func isZero(b []byte) bool {
	for _, x := range b {
		if x != 0 {
			return false
		}
	}
	return true
}

// ---------------------------------------------------------------------
// Transactions (three arms reused from the M-T generator, reshaped to the
// v3-block output forms)
// ---------------------------------------------------------------------

// buildAccessList converts the input access list into (a) the geth form for
// signing/execution and (b) the output mirror. The geth list is always
// non-nil (empty list, same RLP as before the field existed); the output
// mirror is nil when empty so omitempty keeps the old 25 vectors byte-stable.
// A tuple's nil StorageKeys is normalized to [] on both sides (the replayer
// iterates storageKeys; null is a schema violation).
func buildAccessList(in []outputAccessTuple) (types.AccessList, []outputAccessTuple) {
	al := types.AccessList{}
	var out []outputAccessTuple
	for _, t := range in {
		keys := t.StorageKeys
		if keys == nil {
			keys = []common.Hash{}
		}
		al = append(al, types.AccessTuple{Address: t.Address, StorageKeys: keys})
		out = append(out, outputAccessTuple{Address: t.Address, StorageKeys: keys})
	}
	return al, out
}

// authStructurallyUnrecoverable mirrors the replayer's predicate verbatim
// (OpT8nReplayTest.cpp structurallyUnrecoverable: secp256k1 EIP-2/EIP-7702
// malleability boundary, no ecrecover). Same predicate on both sides so a
// (b)-class corpus authoring error is stopped at generation time.
var (
	secpN     = crypto.S256().Params().N
	secpHalfN = new(big.Int).Rsh(crypto.S256().Params().N, 1)
)

func authStructurallyUnrecoverable(a types.SetCodeAuthorization) bool {
	r, s := a.R.ToBig(), a.S.ToBig()
	return a.V > 1 || s.Cmp(secpHalfN) > 0 || r.Sign() == 0 || r.Cmp(secpN) >= 0 ||
		s.Sign() == 0 || s.Cmp(secpN) >= 0
}

func buildTx(in *inputTx, signer types.Signer, cfg *params.ChainConfig) (*types.Transaction, json.RawMessage, error) {
	switch in.OpType {
	case "deposit":
		if in.OpDeposit == nil {
			return nil, nil, fmt.Errorf(`_op_type="deposit" requires "_op_deposit"`)
		}
		d := in.OpDeposit
		var mint *big.Int
		if d.Mint != nil {
			mint = (*big.Int)(d.Mint)
		}
		value := big.NewInt(0)
		if d.Value != nil {
			value = (*big.Int)(d.Value)
		}
		tx := types.NewTx(&types.DepositTx{
			SourceHash:          d.SourceHash,
			From:                d.From,
			To:                  d.To,
			Mint:                mint,
			Value:               value,
			Gas:                 uint64(d.Gas),
			IsSystemTransaction: d.IsSystemTx,
			Data:                []byte(in.Data),
		})
		outJSON, err := json.Marshal(outputDepositTx{
			OpType:    "deposit",
			OpDeposit: *d,
			Data:      in.Data,
		})
		return tx, outJSON, err

	case "eip1559":
		prv, from, err := parseKey(in.SecretKey)
		if err != nil {
			return nil, nil, err
		}
		chainID, nonce, gas, value, tip, feeCap := txScalars(in)
		accessList, outAccessList := buildAccessList(in.AccessList)
		txdata := &types.DynamicFeeTx{
			ChainID:    chainID,
			Nonce:      nonce,
			GasTipCap:  tip,
			GasFeeCap:  feeCap,
			Gas:        gas,
			To:         in.To,
			Value:      value,
			Data:       []byte(in.Data),
			AccessList: accessList,
		}
		tx, err := types.SignNewTx(prv, signer, txdata)
		if err != nil {
			return nil, nil, fmt.Errorf("signing eip1559 tx: %w", err)
		}
		rawBin, err := tx.MarshalBinary() // iron rule: _op_raw = tx.MarshalBinary()
		if err != nil {
			return nil, nil, err
		}
		outJSON, err := json.Marshal(outputSignedTx{
			OpType:               "eip1559",
			OpRaw:                hexutil.Encode(rawBin),
			ChainID:              (*math.HexOrDecimal256)(chainID),
			Nonce:                math.HexOrDecimal64(nonce),
			To:                   in.To,
			Gas:                  math.HexOrDecimal64(gas),
			MaxFeePerGas:         (*math.HexOrDecimal256)(feeCap),
			MaxPriorityFeePerGas: (*math.HexOrDecimal256)(tip),
			Value:                (*math.HexOrDecimal256)(value),
			Data:                 in.Data,
			AccessList:           outAccessList,
			Sender:               from,
		})
		return tx, outJSON, err

	case "setcode":
		prv, from, err := parseKey(in.SecretKey)
		if err != nil {
			return nil, nil, err
		}
		if in.To == nil {
			return nil, nil, fmt.Errorf(`_op_type="setcode" requires "to" (EIP-7702 txs cannot create contracts)`)
		}
		if len(in.OpAuthorizations) == 0 {
			return nil, nil, fmt.Errorf(`_op_type="setcode" requires a non-empty "_op_authorization_list"`)
		}
		chainID, nonce, gas, value, tip, feeCap := txScalars(in)
		accessList, outAccessList := buildAccessList(in.AccessList)
		authList := make([]types.SetCodeAuthorization, len(in.OpAuthorizations))
		outAuths := make([]outputAuthorization, len(in.OpAuthorizations))
		for i, a := range in.OpAuthorizations {
			authChainID := big.NewInt(0)
			if a.ChainID != nil {
				authChainID = (*big.Int)(a.ChainID)
			}
			overridePresent := a.RawYParity != nil || a.RawR != nil || a.RawS != nil
			var auth types.SetCodeAuthorization
			if overridePresent {
				// Raw override: structurally-bad signature, taken verbatim,
				// NOT signed. All three fields must come together, and the
				// tuple must not also carry a signing key.
				if a.RawYParity == nil || a.RawR == nil || a.RawS == nil {
					return nil, nil, fmt.Errorf("_op_authorization_list[%d]: partial raw override (rawYParity/rawR/rawS must appear together)", i)
				}
				if len(a.AuthSecretKey) != 0 {
					return nil, nil, fmt.Errorf("_op_authorization_list[%d]: raw override and authSecretKey are mutually exclusive", i)
				}
				if *a.RawYParity > 255 {
					return nil, nil, fmt.Errorf("_op_authorization_list[%d]: rawYParity %d does not fit uint8", i, *a.RawYParity)
				}
				auth = types.SetCodeAuthorization{
					ChainID: *uint256.MustFromBig(authChainID),
					Address: a.Address,
					Nonce:   uint64(a.Nonce),
					V:       uint8(*a.RawYParity),
					R:       *uint256.MustFromBig((*big.Int)(a.RawR)),
					S:       *uint256.MustFromBig((*big.Int)(a.RawS)),
				}
			} else {
				authPrv, err := crypto.ToECDSA(a.AuthSecretKey)
				if err != nil {
					return nil, nil, fmt.Errorf("_op_authorization_list[%d]: %w", i, err)
				}
				auth, err = types.SignSetCode(authPrv, types.SetCodeAuthorization{
					ChainID: *uint256.MustFromBig(authChainID),
					Address: a.Address,
					Nonce:   uint64(a.Nonce),
				})
				if err != nil {
					return nil, nil, fmt.Errorf("_op_authorization_list[%d]: SignSetCode: %w", i, err)
				}
			}
			// Three-way consistency assertion (iron rule): marker <=> override
			// present <=> structural predicate true. Any mismatch is a corpus
			// authoring error and stops generation ((b)-class, same predicate
			// as the replayer).
			pred := authStructurallyUnrecoverable(auth)
			if a.SignerUnrecoverable != overridePresent || overridePresent != pred {
				return nil, nil, fmt.Errorf(
					"_op_authorization_list[%d]: marker/override/predicate mismatch (marker=%v, override=%v, structurally-unrecoverable=%v)",
					i, a.SignerUnrecoverable, overridePresent, pred)
			}
			authList[i] = auth
			outAuths[i] = outputAuthorization{
				ChainID:             (*math.HexOrDecimal256)(authChainID),
				Address:             a.Address,
				Nonce:               a.Nonce,
				YParity:             math.HexOrDecimal64(auth.V),
				R:                   (*math.HexOrDecimal256)(auth.R.ToBig()),
				S:                   (*math.HexOrDecimal256)(auth.S.ToBig()),
				SignerUnrecoverable: a.SignerUnrecoverable,
			}
		}
		txdata := &types.SetCodeTx{
			ChainID:    uint256.MustFromBig(chainID),
			Nonce:      nonce,
			GasTipCap:  uint256.MustFromBig(tip),
			GasFeeCap:  uint256.MustFromBig(feeCap),
			Gas:        gas,
			To:         *in.To,
			Value:      uint256.MustFromBig(value),
			Data:       []byte(in.Data),
			AccessList: accessList,
			AuthList:   authList,
		}
		tx, err := types.SignNewTx(prv, signer, txdata)
		if err != nil {
			return nil, nil, fmt.Errorf("signing setcode tx: %w", err)
		}
		rawBin, err := tx.MarshalBinary()
		if err != nil {
			return nil, nil, err
		}
		outJSON, err := json.Marshal(outputSetCodeTx{
			OpType:               "setcode",
			OpRaw:                hexutil.Encode(rawBin),
			ChainID:              (*math.HexOrDecimal256)(chainID),
			Nonce:                math.HexOrDecimal64(nonce),
			To:                   *in.To,
			Gas:                  math.HexOrDecimal64(gas),
			MaxFeePerGas:         (*math.HexOrDecimal256)(feeCap),
			MaxPriorityFeePerGas: (*math.HexOrDecimal256)(tip),
			Value:                (*math.HexOrDecimal256)(value),
			Data:                 in.Data,
			AccessList:           outAccessList,
			Sender:               from,
			OpAuthorizationList:  outAuths,
		})
		return tx, outJSON, err

	default:
		return nil, nil, fmt.Errorf("unknown _op_type %q", in.OpType)
	}
}

func parseKey(k *hexutil.Bytes) (*ecdsa.PrivateKey, common.Address, error) {
	if k == nil {
		return nil, common.Address{}, fmt.Errorf("signed tx requires secretKey")
	}
	key, err := crypto.ToECDSA(*k)
	if err != nil {
		return nil, common.Address{}, fmt.Errorf("parsing secretKey: %w", err)
	}
	return key, crypto.PubkeyToAddress(key.PublicKey), nil
}

func txScalars(in *inputTx) (chainID *big.Int, nonce, gas uint64, value, tip, feeCap *big.Int) {
	chainID = big.NewInt(0)
	if in.ChainID != nil {
		chainID = (*big.Int)(in.ChainID)
	}
	if in.Nonce != nil {
		nonce = uint64(*in.Nonce)
	}
	if in.Gas != nil {
		gas = uint64(*in.Gas)
	}
	value, tip, feeCap = big.NewInt(0), big.NewInt(0), big.NewInt(0)
	if in.Value != nil {
		value = (*big.Int)(in.Value)
	}
	if in.MaxPriorityFeePerGas != nil {
		tip = (*big.Int)(in.MaxPriorityFeePerGas)
	}
	if in.MaxFeePerGas != nil {
		feeCap = (*big.Int)(in.MaxFeePerGas)
	}
	return
}

// ---------------------------------------------------------------------
// postState: candidate-set full emission + trie completeness check
// ---------------------------------------------------------------------

type addrSet struct{ m map[common.Address]struct{} }

func newAddrSet() *addrSet              { return &addrSet{m: map[common.Address]struct{}{}} }
func (s *addrSet) add(a common.Address) { s.m[a] = struct{}{} }
func (s *addrSet) sorted() []common.Address {
	out := make([]common.Address, 0, len(s.m))
	for a := range s.m {
		out = append(out, a)
	}
	sort.Slice(out, func(i, j int) bool { return bytes.Compare(out[i][:], out[j][:]) < 0 })
	return out
}

// buildSlotCandidates returns, per account, the author-known slot set
// (decision record 8): slots declared in pre, slots declared in
// extra_storage, and the analytically-known EIP-4788/2935 ring slots for the
// generated block (written by the system calls, outside any tx).
func buildSlotCandidates(in *inputCase, blockTime, blockNumber uint64) map[common.Address]map[common.Hash]struct{} {
	out := map[common.Address]map[common.Hash]struct{}{}
	addSlot := func(addr common.Address, slot common.Hash) {
		if out[addr] == nil {
			out[addr] = map[common.Hash]struct{}{}
		}
		out[addr][slot] = struct{}{}
	}
	for addr, acc := range in.Pre {
		for slot := range acc.Storage {
			addSlot(addr, slot)
		}
	}
	for addr, slots := range in.ExtraStorage {
		for _, slot := range slots {
			addSlot(addr, slot)
		}
	}
	// EIP-4788: timestamp % 8191 (timestamp) and + 8191 (root).
	addSlot(beaconRootsAddr, common.BigToHash(new(big.Int).SetUint64(blockTime%ringSize)))
	addSlot(beaconRootsAddr, common.BigToHash(new(big.Int).SetUint64(blockTime%ringSize+ringSize)))
	// EIP-2935: (number-1) % 8191 <- parent hash.
	addSlot(historyStorage, common.BigToHash(new(big.Int).SetUint64((blockNumber-1)%ringSize)))
	return out
}

// emitPostState opens the post-block state from the generation DB, verifies
// via secure-trie iteration that every existing account is in the candidate
// set and every storage slot of every candidate is in its declared slot set
// (hashed-key comparison, no preimages needed), then emits the full candidate
// set: for every candidate account its balance/nonce/code and all non-zero
// declared slots. A candidate that does not exist post-block is emitted as
// the trie-semantic zero account {"balance":"0x0"}.
func emitPostState(db ethdb.Database, root common.Hash, candidates *addrSet, slotCands map[common.Address]map[common.Hash]struct{}) (types.GenesisAlloc, *state.StateDB, error) {
	tdb := triedb.NewDatabase(db, triedb.HashDefaults)
	sdb := state.NewDatabase(tdb, nil)
	statedb, err := state.New(root, sdb)
	if err != nil {
		return nil, nil, fmt.Errorf("open post state: %w", err)
	}
	tr, err := sdb.OpenTrie(root)
	if err != nil {
		return nil, nil, fmt.Errorf("open account trie: %w", err)
	}

	hashedToAddr := map[common.Hash]common.Address{}
	for addr := range candidates.m {
		hashedToAddr[common.BytesToHash(crypto.Keccak256(addr[:]))] = addr
	}

	nodeIt, err := tr.NodeIterator(nil)
	if err != nil {
		return nil, nil, err
	}
	accounts := map[common.Address]types.StateAccount{}
	it := trie.NewIterator(nodeIt)
	for it.Next() {
		addr, ok := hashedToAddr[common.BytesToHash(it.Key)]
		if !ok {
			return nil, nil, fmt.Errorf("post-state account outside candidate set (hashed key %x) -- corpus must list every written address (extra_candidates)", it.Key)
		}
		var acct types.StateAccount
		if err := rlp.DecodeBytes(it.Value, &acct); err != nil {
			return nil, nil, fmt.Errorf("decode account %s: %w", addr, err)
		}
		accounts[addr] = acct
	}
	if it.Err != nil {
		return nil, nil, fmt.Errorf("account trie iteration: %w", it.Err)
	}

	out := types.GenesisAlloc{}
	for _, addr := range candidates.sorted() {
		acct, exists := accounts[addr]
		storage := map[common.Hash]common.Hash{}
		if exists && acct.Root != types.EmptyRootHash {
			hashedToSlot := map[common.Hash]common.Hash{}
			for slot := range slotCands[addr] {
				hashedToSlot[common.BytesToHash(crypto.Keccak256(slot[:]))] = slot
			}
			str, err := sdb.OpenStorageTrie(root, addr, acct.Root, tr)
			if err != nil {
				return nil, nil, fmt.Errorf("open storage trie of %s: %w", addr, err)
			}
			snIt, err := str.NodeIterator(nil)
			if err != nil {
				return nil, nil, err
			}
			sit := trie.NewIterator(snIt)
			for sit.Next() {
				slot, ok := hashedToSlot[common.BytesToHash(sit.Key)]
				if !ok {
					return nil, nil, fmt.Errorf("account %s has a storage slot outside the declared slot set (hashed key %x) -- corpus must list every written slot (pre storage / extra_storage)", addr, sit.Key)
				}
				_, content, _, err := rlp.Split(sit.Value)
				if err != nil {
					return nil, nil, fmt.Errorf("decode storage value of %s: %w", addr, err)
				}
				storage[slot] = common.BytesToHash(content)
			}
			if sit.Err != nil {
				return nil, nil, fmt.Errorf("storage trie iteration of %s: %w", addr, sit.Err)
			}
		}
		acc := types.Account{
			Balance: statedb.GetBalance(addr).ToBig(),
			Nonce:   statedb.GetNonce(addr),
		}
		if code := statedb.GetCode(addr); len(code) > 0 {
			acc.Code = code
		}
		if len(storage) > 0 {
			acc.Storage = storage
		}
		out[addr] = acc
	}
	return out, statedb, nil
}

// ---------------------------------------------------------------------
// Receipt expectations + fee cross-checks
// ---------------------------------------------------------------------

// preStateGetter adapts the case's pre alloc to types.StateGetter, so the
// generator's own operator-fee math can be cross-checked against op-geth's
// NewOperatorCostFunc over the exact same slot-8 params. (结构性保证：语料规则
// 禁止 L1Block 携 code，块内无路径可写其槽 -- so pre == the state the
// lazily-initialized cost funcs observe.)
type preStateGetter types.GenesisAlloc

func (p preStateGetter) GetState(addr common.Address, slot common.Hash) common.Hash {
	acc, ok := types.GenesisAlloc(p)[addr]
	if !ok {
		return common.Hash{}
	}
	return acc.Storage[slot]
}

// operatorFee implements the fork formulas from rollup_cost.go:254-287:
//
//	Isthmus: gasUsed*scalar/1e6 + constant
//	Jovian:  gasUsed*scalar*100 + constant
func operatorFee(jovian bool, gasUsed uint64, scalar uint32, constant uint64) *big.Int {
	fee := new(big.Int).SetUint64(gasUsed)
	fee.Mul(fee, new(big.Int).SetUint64(uint64(scalar)))
	if jovian {
		fee.Mul(fee, big.NewInt(100))
	} else {
		fee.Div(fee, big.NewInt(1_000_000))
	}
	return fee.Add(fee, new(big.Int).SetUint64(constant))
}

func buildExpectedReceipts(cfg *params.ChainConfig, in *inputCase, txs []*types.Transaction, receipts types.Receipts, blockTime uint64) ([]expectedReceipt, error) {
	jovian := cfg.IsJovian(blockTime)
	slot8 := preStateGetter(in.Pre).GetState(l1BlockAddr, types.OperatorFeeParamsSlot)
	opScalar := binary.BigEndian.Uint32(slot8[20:24])
	opConstant := binary.BigEndian.Uint64(slot8[24:32])
	emitOpFee := opScalar != 0 || opConstant != 0 // presence mirrors deriveOPStackFields
	refOpCost := types.NewOperatorCostFunc(cfg, preStateGetter(in.Pre))

	out := make([]expectedReceipt, len(receipts))
	var cumulative uint64
	for i, r := range receipts {
		if r.CumulativeGasUsed < cumulative {
			return nil, fmt.Errorf("receipt %d: non-monotonic cumulative gas", i)
		}
		cumulative = r.CumulativeGasUsed
		er := expectedReceipt{
			Type:              hexutil.EncodeUint64(uint64(r.Type)),
			Status:            hexutil.EncodeUint64(r.Status),
			GasUsed:           hexutil.EncodeUint64(r.GasUsed),
			CumulativeGasUsed: hexutil.EncodeUint64(r.CumulativeGasUsed),
			LogsCount:         len(r.Logs),
		}
		if r.DepositNonce != nil {
			s := hexutil.EncodeUint64(*r.DepositNonce)
			er.OpDepositNonce = &s
		}
		if r.DepositReceiptVersion != nil {
			s := hexutil.EncodeUint64(*r.DepositReceiptVersion)
			er.OpDepositReceiptVersion = &s
		}
		if !txs[i].IsDepositTx() {
			if r.L1Fee == nil {
				return nil, fmt.Errorf("receipt %d: non-deposit tx missing L1Fee after DeriveFields", i)
			}
			s := hexutil.EncodeBig(r.L1Fee)
			er.OpL1Fee = &s
			// 线 C：字段发射面按 OP_RECEIPT_FIELDMAP.md（nil → absent → omitempty）。
			if r.L1GasPrice != nil {
				s := hexutil.EncodeBig(r.L1GasPrice)
				er.OpL1GasPrice = &s
			}
			if r.L1BlobBaseFee != nil {
				s := hexutil.EncodeBig(r.L1BlobBaseFee)
				er.OpL1BlobBaseFee = &s
			}
			if r.L1GasUsed != nil {
				s := hexutil.EncodeBig(r.L1GasUsed)
				er.OpL1GasUsed = &s
			}
			if r.L1BaseFeeScalar != nil {
				s := hexutil.EncodeUint64(*r.L1BaseFeeScalar)
				er.OpL1BaseFeeScalar = &s
			}
			if r.L1BlobBaseFeeScalar != nil {
				s := hexutil.EncodeUint64(*r.L1BlobBaseFeeScalar)
				er.OpL1BlobBaseFeeScalar = &s
			}
			if r.OperatorFeeScalar != nil {
				s := hexutil.EncodeUint64(*r.OperatorFeeScalar)
				er.OpOperatorFeeScalar = &s
			}
			if r.OperatorFeeConstant != nil {
				s := hexutil.EncodeUint64(*r.OperatorFeeConstant)
				er.OpOperatorFeeConstant = &s
			}
			if r.DAFootprintGasScalar != nil {
				s := hexutil.EncodeUint64(*r.DAFootprintGasScalar)
				er.OpDaFootprintGasScalar = &s
			}
			if emitOpFee {
				fee := operatorFee(jovian, r.GasUsed, opScalar, opConstant)
				// Cross-check the hand formula against op-geth's own cost func.
				if ref := refOpCost(r.GasUsed, blockTime); ref.ToBig().Cmp(fee) != 0 {
					return nil, fmt.Errorf("receipt %d: operator fee formula mismatch: generator %s vs op-geth %s", i, fee, ref)
				}
				sf := hexutil.EncodeBig(fee)
				er.OpOperatorFee = &sf
			}
			if jovian {
				sd := hexutil.EncodeUint64(r.BlobGasUsed) // deriveOPStackFields: daScalar * EstimatedDASize
				er.OpDaFootprint = &sd
			}
		}
		out[i] = er
	}
	return out, nil
}

// crossCheckVaults asserts sum(per-tx op fee) and sum(per-tx L1 fee) against
// the OperatorFeeVault / L1FeeVault balance deltas of the actually-executed
// block -- i.e. the emitted receipt expectations are provably the values the
// state transition charged, not merely re-derived from the same inputs.
func crossCheckVaults(in *inputCase, exp []expectedReceipt, post *state.StateDB) error {
	sumOp, sumL1 := new(big.Int), new(big.Int)
	for i := range exp {
		if exp[i].OpOperatorFee != nil {
			v, err := hexutil.DecodeBig(*exp[i].OpOperatorFee)
			if err != nil {
				return err
			}
			sumOp.Add(sumOp, v)
		}
		if exp[i].OpL1Fee != nil {
			v, err := hexutil.DecodeBig(*exp[i].OpL1Fee)
			if err != nil {
				return err
			}
			sumL1.Add(sumL1, v)
		}
	}
	delta := func(addr common.Address) *big.Int {
		preBal := big.NewInt(0)
		if acc, ok := in.Pre[addr]; ok && acc.Balance != nil {
			preBal = acc.Balance
		}
		return new(big.Int).Sub(post.GetBalance(addr).ToBig(), preBal)
	}
	if d := delta(operatorFeeVault); d.Cmp(sumOp) != 0 {
		return fmt.Errorf("operator fee cross-check: vault delta %s != sum of per-tx fees %s", d, sumOp)
	}
	if d := delta(l1FeeVault); d.Cmp(sumL1) != 0 {
		return fmt.Errorf("l1 fee cross-check: vault delta %s != sum of per-tx L1 fees %s", d, sumL1)
	}
	return nil
}

// ---------------------------------------------------------------------
// Dev probe: ring-wrap feasibility (case 11 note). Genesis.Commit refuses
// number > 0 (op-geth core/genesis.go:728) -- this probe demonstrates it at
// runtime so the README's "wrap has no discriminating case" note is
// evidence-backed, not read-only.
// ---------------------------------------------------------------------

func probeGenesisNumber() {
	cfg, _ := buildChainConfig("isthmus")
	zero := uint64(0)
	_ = zero
	genesis := &core.Genesis{
		Config:     cfg,
		Timestamp:  1000,
		GasLimit:   10_000_000,
		Difficulty: big.NewInt(0),
		ExtraData:  eip1559.EncodeOptimismExtraData(cfg, 1000, 50, 6, nil),
		Alloc:      types.GenesisAlloc{},
		Number:     ringSize, // 8191 -> block 8192 would wrap the 2935 ring
	}
	defer func() {
		if r := recover(); r != nil {
			fmt.Printf("probe: Genesis.Number=8191 REJECTED (panic: %v)\n", r)
		}
	}()
	_, blocks, _ := core.GenerateChainWithGenesis(genesis, beacon.New(ethash.NewFaker()), 1, nil)
	fmt.Printf("probe: unexpectedly generated block %d\n", blocks[0].NumberU64())
}
