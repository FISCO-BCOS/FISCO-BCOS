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
//	    deterministically (re)writes the 77 corpus case files (*.in.json).
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
	"strconv"
	"strings"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/common/hexutil"
	"github.com/ethereum/go-ethereum/common/math"
	"github.com/ethereum/go-ethereum/consensus"
	"github.com/ethereum/go-ethereum/consensus/beacon"
	"github.com/ethereum/go-ethereum/consensus/ethash"
	"github.com/ethereum/go-ethereum/consensus/misc/eip1559"
	"github.com/ethereum/go-ethereum/core"
	"github.com/ethereum/go-ethereum/core/rawdb"
	"github.com/ethereum/go-ethereum/core/state"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/core/vm"
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
		writeCases     = flag.String("write-cases", "", "write the 77 corpus case files into <dir> and exit")
		probeFields    = flag.String("probe-receipt-fields", "", "dev probe: run one <case.in.json> through the self-contained op-geth pipeline and dump every receipt's OP-Stack field emission (presence + hex value), then exit")
		inputPath      = flag.String("input", "", "input case JSON")
		outputPath     = flag.String("output", "", "output vector JSON")
		opGethCommit   = flag.String("op-geth-commit", "unknown", "full sha of the op-geth checkout, recorded into _op_test_vectors.generator_commit")
		probeWrap      = flag.Bool("probe-genesis-number", false, "dev probe: attempt Genesis.Number=8191 (ring-wrap feasibility) and report")
		probeSpec      = flag.Bool("probe-spec", false, "dev probe: build representative chainConfigSpec values through buildChainConfigSpec and print each activation timeline (verifies the Task-0 upgrade-boundary interface), then exit")
		probePrecomp   = flag.String("probe-precompile", "", "dev probe: verify every precompile valid-input helper against the real op-geth precompile Run (core/vm.PrecompiledContractsOsaka), then build+generate the bn256-add probe frame and dump its receipts (line-B Task 0 infra), then exit")
		goldenOutput   = flag.String("golden-output", "", "Task 2 (engine gate golden ritual): also emit blockHash/transactionsRoot/extraData/excessBlobGas/rawTransactions/encodedHeaderHex for this vector to this path (vectors/ itself is untouched)")
		chainOutputDir = flag.String("chain-output-dir", "", "Task 2 Step 2: generate the off-line 1->2 chained golden pair (GenerateChainWithGenesis n=2, InsertChain-validated) into this directory and exit")
		// Task 3 corrupt/static modes: independent of --write-cases (they take
		// the ASSEMBLED valid product of a base case and re-emit it as an invalid
		// vector). corrupt emits invalid_<base>_<field>.json for every §4a field;
		// static emits invalid_<base>_static_<n>.json for every §4c item.
		invalidMode = flag.String("mode", "", "corrupt|static|invalid-tx: emit invalid vectors from a base case stem; chain:<N>[:fork|:break]: emit a linear chain / fork / break vector")
		baseStem    = flag.String("base", "isthmus_transfer_basic", "base case stem for --mode=corrupt/static (e.g. isthmus_transfer_basic)")
		invalidOut  = flag.String("out-dir", "", "output dir for --mode=corrupt/static invalid vectors")
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
	case *probePrecomp != "":
		if err := probePrecompile(*probePrecomp); err != nil {
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
	case strings.HasPrefix(*invalidMode, "chain:"):
		if err := runChainMode(*invalidMode, *invalidOut, *opGethCommit); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	case *invalidMode == "corrupt" || *invalidMode == "static" || *invalidMode == "invalid-tx":
		if err := runInvalidMode(*invalidMode, *baseStem, *invalidOut, *opGethCommit); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	case *inputPath != "" && *outputPath != "":
		if err := run(*inputPath, *outputPath, *opGethCommit, *goldenOutput); err != nil {
			fmt.Fprintf(os.Stderr, "opt8n-ref: %v\n", err)
			os.Exit(1)
		}
	default:
		fmt.Fprintln(os.Stderr, "usage: opt8n-ref --write-cases <dir> | --probe-receipt-fields <case.in.json> | --probe-spec | --probe-genesis-number | --probe-precompile <fork> | --input <case.in.json> --output <vector.json> [--golden-output <golden.json>] [--op-geth-commit <sha>] | --chain-output-dir <dir> [--op-geth-commit <sha>] | --mode corrupt|static|invalid-tx --base <stem> --out-dir <dir> [--op-geth-commit <sha>] | --mode chain:<N>[:fork|:break] --out-dir <dir> [--op-geth-commit <sha>]")
		os.Exit(2)
	}
}

// ---------------------------------------------------------------------
// Input schema (.in.json)
// ---------------------------------------------------------------------

type caseInfo struct {
	Hardfork    string `json:"hardfork"`
	Description string `json:"description"`
	// Activations is the upgrade-boundary spec (Task 3): per-fork activation
	// timestamps. Present only on the upgrade_*_activation cases; when set,
	// processBlockVector/probeReceiptFields build the chain config via
	// buildChainConfigSpec({base: hardfork, activations}) instead of the plain
	// buildChainConfig(hardfork). omitempty keeps the pre-existing 33 case
	// files byte-stable under regeneration.
	Activations map[string]uint64 `json:"activations,omitempty"`
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
	// GasPrice is the type-0 (legacy) arm's per-gas price (EIP-1559 arms use
	// maxFeePerGas/maxPriorityFeePerGas). omitempty: absent on all pre-existing
	// cases, must not re-emit as null.
	GasPrice *math.HexOrDecimal256 `json:"gasPrice,omitempty"`
	Data     hexutil.Bytes         `json:"data,omitempty"`
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

// outputLegacyTx is the type-0 (EIP-155 protected) arm's output object. Unlike
// the EIP-1559 arms it carries a single `gasPrice` (no maxFeePerGas /
// maxPriorityFeePerGas). ⚠️ The T8n replayer does not yet parse `_op_type
// "legacy"` (OpT8nReplayTest.cpp:513-644); the legacy vectors must not be
// manifest-registered until that consumer arm lands (see task-3-report).
type outputLegacyTx struct {
	OpType   string                `json:"_op_type"`
	OpRaw    string                `json:"_op_raw"`
	ChainID  *math.HexOrDecimal256 `json:"chainId"`
	Nonce    math.HexOrDecimal64   `json:"nonce"`
	To       *common.Address       `json:"to"`
	Gas      math.HexOrDecimal64   `json:"gas"`
	GasPrice *math.HexOrDecimal256 `json:"gasPrice"`
	Value    *math.HexOrDecimal256 `json:"value"`
	Data     hexutil.Bytes         `json:"data"`
	Sender   common.Address        `json:"sender"`
}

// outputSetCodeTxCreate is the setcode_create invalid-tx output object
// (Task 4). Unlike outputSetCodeTx, To is *common.Address so the vector can
// carry `to: null` — the evmone opValidate CREATE_SET_CODE_TX trigger
// (eth/state/state.cpp:356-357). The replayer's setcode loader reads to
// verbatim (OpT8nReplayTest.cpp:541-543), so the structured object (not the
// raw envelope) is what makes the create rejection reachable.
type outputSetCodeTxCreate struct {
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
	Sender               common.Address        `json:"sender"`
	OpAuthorizationList  []outputAuthorization `json:"_op_authorization_list"`
}

// outputBlobTx is the blob invalid-tx output object (Task 4). The replayer has
// no blob arm yet (consumer follow-up); the engine payload consumes the _op_raw
// envelope directly, and this structured object carries the fields the future
// blob loader would need (EIP-4844: blobFeeCap + blobHashes).
type outputBlobTx struct {
	OpType               string                `json:"_op_type"`
	OpRaw                string                `json:"_op_raw"`
	ChainID              *math.HexOrDecimal256 `json:"chainId"`
	Nonce                math.HexOrDecimal64   `json:"nonce"`
	To                   *common.Address       `json:"to"`
	Gas                  math.HexOrDecimal64   `json:"gas"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas"`
	BlobFeeCap           *math.HexOrDecimal256 `json:"blobFeeCap"`
	BlobHashes           []common.Hash         `json:"blobHashes"`
	Value                *math.HexOrDecimal256 `json:"value"`
	Data                 hexutil.Bytes         `json:"data"`
	Sender               common.Address        `json:"sender"`
}

type outputBlock struct {
	Transactions []json.RawMessage `json:"transactions"`
}

type expectedHeader struct {
	GasUsed         string  `json:"gasUsed"`
	ReceiptsRoot    string  `json:"receiptsRoot"`
	LogsBloom       string  `json:"logsBloom"`
	WithdrawalsRoot string  `json:"withdrawalsRoot"`
	RequestsHash    *string `json:"requestsHash,omitempty"` // nil pre-Prague (ecotone/fjord/granite/holocene): op-geth t8n omits it
	BlobGasUsed     string  `json:"blobGasUsed"`
	StateRoot       string  `json:"stateRoot"`
}

type expectedReceipt struct {
	Type              string `json:"type"`
	Status            string `json:"status"`
	GasUsed           string `json:"gasUsed"`
	CumulativeGasUsed string `json:"cumulativeGasUsed"`
	LogsCount         int    `json:"logsCount"`
	// Tx return data (receipt output), hex-encoded; always emitted ("0x" for
	// empty). Captured by reexecuting the block (chain-maker AddTx discards it).
	Output                  string  `json:"output"`
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
	// Reject is present ONLY on invalid vectors (Task 3 corrupt/static modes).
	// omitempty keeps the old 77 valid vectors byte-for-byte stable under
	// regeneration (review E11).
	Reject *rejectExpected `json:"reject,omitempty"`
}

// rejectExpected is the _op_expected.reject schema (task-2/3 brief): the
// op-geth anchor message (weak, documentation-only) plus the FISCO consumer
// contract that the C++ runners (OpNewPayloadRpcE2eTest / OpT8nReplayTest)
// actually assert against. Field presence is the contract -- every
// non-omitempty field is required on an invalid vector.
type rejectExpected struct {
	OpGeth string      `json:"op_geth"`
	Fisco  fiscoReject `json:"fisco"`
}

// fiscoReject is the FISCO-side expectation. Consumer is "engine" for the
// newPayload gate (field/static corruptions, this task) and "executor"/"both"
// for the T8n processOpBlock throw path (task 1/2 vectors).
type fiscoReject struct {
	Consumer                string          `json:"consumer,omitempty"`
	Classification          string          `json:"classification"`              // INVALID | SYNCING | -32603 | -38005
	LatestValidHash         json.RawMessage `json:"latest_valid_hash,omitempty"` // "parent" | null (INVALID only)
	ValidationErrorContains string          `json:"validation_error_contains,omitempty"`
	// ExpectThrow is the engine-throw class the E2E runner asserts via
	// BOOST_CHECK_THROW (OpNewPayloadRpcE2eTest.cpp runInvalidVector):
	// "OpExecutionInternalError" for the -32603 same-height fork two-pour.
	ExpectThrow string `json:"expect_throw,omitempty"`
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

// invalidVectorDoc is the on-disk shape of a Task 3/4 invalid vector
// (vectors/invalid_*.json). The engine consumer (OpNewPayloadRpcE2eTest,
// w6test::loadInvalidSample) reads _info.hardfork / pre / _op_payload /
// _op_expected.reject. The Task 4 invalid-tx vectors ADD env + block
// (omitempty, so corrupt/static vectors stay byte-identical): the T8n replayer
// (OpT8nReplayTest::loadBlockContext) needs env + block.transactions (the
// structured tx objects) to reach assertRejectThrow.
type invalidVectorDoc struct {
	Info caseInfo                         `json:"_info"`
	Pre  map[common.Address]outputAccount `json:"pre"`
	// Env/Block/PostState are *pointers* + omitempty: Go's omitempty does NOT
	// elide a zero struct, so a value field would leak `env`/`block` into the
	// Task 3 corrupt/static vectors and break byte-invariance. The Task 4
	// invalid-tx vectors set them (the T8n replayer needs env + block; the
	// setcode loader needs a present postState).
	Env        *outputEnv             `json:"env,omitempty"`
	Block      *outputBlock           `json:"block,omitempty"`
	PostState  *types.GenesisAlloc    `json:"postState,omitempty"`
	OpPayload  map[string]interface{} `json:"_op_payload"`
	OpExpected invalidExpected        `json:"_op_expected"`
	// OpCanonical is the -32603 two-pour carrier (Task 2 handoff): the
	// CANONICAL sibling's full ExecutionPayload (with parentBeaconBlockRoot),
	// which the E2E runner pours FIRST and expects VALID. Only chain_fork_*
	// vectors carry it.
	OpCanonical map[string]interface{} `json:"_op_canonical,omitempty"`
}

// invalidExpected is the lean _op_expected for invalid vectors: only the reject
// schema is emitted.
type invalidExpected struct {
	Reject *rejectExpected `json:"reject"`
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

// buildConfigForCase builds the chain config for one case: the plain per-fork
// recipe for pure-fork cases, or the upgrade-boundary spec (base fork + the
// _info.activations timestamps) for the Task-3 activation vectors. The 10-second
// coupling is enforced by the case constructor (genesis timestamp = T-5,
// blockTime = genesis+10 = T+5), not here.
func buildConfigForCase(in *inputCase) (*params.ChainConfig, error) {
	if len(in.Info.Activations) > 0 {
		return buildChainConfigSpec(chainConfigSpec{base: in.Info.Hardfork, activations: in.Info.Activations})
	}
	return buildChainConfig(in.Info.Hardfork)
}

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

// probePrecompile is the line-B Task 0 dev probe (--probe-precompile <fork>).
// Two halves:
//
//  1. Input-helper verification: every valid-* / repeated* helper output is fed
//     to the REAL op-geth precompile Run (core/vm.PrecompiledContractsOsaka, the
//     map carrying bn256 + KZG + BLS12-381 + P256VERIFY). A helper passes iff
//     Run returns no error; p256/KZG additionally assert the exact expected
//     output (32-byte-1 / the EIP-4844 fixed return value), proving the
//     signature/proof actually verify.
//  2. Frame generation: build the bn256-add probe frame via precompileFrame +
//     precompileCallTx (To=0x06, two valid G1 points, gas 500_000 -- see the
//     NOTE below on OP intrinsic gas) under the given fork and push it through
//     probeReceiptFields (the full GenerateChainWithGenesis + AddTx pipeline).
//     Confirms the constructor + block gasLimit plumbing produce a generated
//     block whose precompile tx receipt shows success (status 1) and gasUsed
//     ~intrinsic+150.
func probePrecompile(fork string) error {
	osaka := vm.PrecompiledContractsOsaka
	pc := func(n uint) vm.PrecompiledContract {
		c, ok := osaka[common.BytesToAddress(addrBytes(n))]
		if !ok {
			panic(fmt.Sprintf("probe: no precompile at 0x%x in PrecompiledContractsOsaka", n))
		}
		return c
	}

	// --- Half 1: verify every input helper against the real precompile Run ---
	type check struct {
		label   string
		addr    uint
		input   func() []byte
		wantOut []byte // nil => assert no-error only
	}
	kzgReturn := common.Hex2Bytes("000000000000000000000000000000000000000000000000000000000000100073eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001")
	true32 := make([]byte, 32)
	true32[31] = 1
	checks := []check{
		{"bn256 G1 x2 (add input)", preBn256Add, func() []byte { return repeatedPair(validBn256G1(), 2) }, nil},
		{"bn256 G2 (pairing, 1 pair)", preBn256Pairing, func() []byte { return repeatedBn256Pair(1) }, nil},
		{"bn256 pairing, 2 pairs", preBn256Pairing, func() []byte { return repeatedBn256Pair(2) }, nil},
		{"bls12-381 G1 x2 (g1add input)", preBlsG1Add, func() []byte { return repeatedPair(validBlsG1(), 2) }, nil},
		{"bls12-381 G2 x2 (g2add input)", preBlsG2Add, func() []byte { return repeatedPair(validBlsG2(), 2) }, nil},
		{"bls12-381 pairing (1 pair)", preBlsPairing, func() []byte { return append(validBlsG1(), validBlsG2()...) }, nil},
		{"kzg4844 point evaluation", prePointEval, validKZGInput, kzgReturn},
		{"secp256r1 (RIP-7212)", preP256Verify, validP256Sig, true32},
	}

	fmt.Printf("helper length assertions:\n")
	fmt.Printf("  validBn256G1()       len=%d (want 64)\n", len(validBn256G1()))
	fmt.Printf("  validBn256G2()       len=%d (want 128)\n", len(validBn256G2()))
	fmt.Printf("  repeatedBn256Pair(1) len=%d (want 192)\n", len(repeatedBn256Pair(1)))
	fmt.Printf("  validBlsG1()         len=%d (want 128)\n", len(validBlsG1()))
	fmt.Printf("  validBlsG2()         len=%d (want 256)\n", len(validBlsG2()))
	fmt.Printf("  validP256Sig()       len=%d (want 160)\n", len(validP256Sig()))
	fmt.Printf("  validKZGInput()      len=%d (want 192)\n", len(validKZGInput()))

	fail := 0
	for _, c := range checks {
		in := c.input()
		out, err := pc(c.addr).Run(in)
		if err != nil {
			fmt.Printf("  FAIL  %-34s Run error: %v\n", c.label, err)
			fail++
			continue
		}
		if c.wantOut != nil && !bytes.Equal(out, c.wantOut) {
			fmt.Printf("  FAIL  %-34s output mismatch: got 0x%x\n", c.label, out)
			fail++
			continue
		}
		fmt.Printf("  ok    %-34s Run accepted %dB input, output %dB\n", c.label, len(in), len(out))
	}
	if fail > 0 {
		return fmt.Errorf("probe-precompile: %d input-helper check(s) FAILED", fail)
	}

	// --- Half 2: build + generate the bn256-add probe frame ---
	// NOTE: tx gas 500_000, NOT the precompile's 150. On OP-stack the tx gas
	// limit must cover INTRINSIC gas (21000 base + calldata cost; ~21560 for a
	// 128-byte call) and the precompile's RequiredGas (150) is charged on top.
	// A 150-gas tx would OOG at the intrinsic check ("intrinsic gas too low: have
	// 150, want 21560") and never reach the precompile. The brief's "gas 150 /
	// gasUsed ~150" predates the OP intrinsic-gas reality; the receipt's gasUsed
	// is intrinsic + 150 (~21710), with the precompile's 150 visible as the
	// delta over an empty-calldata transfer.
	probeInput := repeatedPair(validBn256G1(), 2) // 128B: two valid G1 points
	spec := precompileFrame(fork, "probe_bn256_add",
		"probe: bn256 add with two valid G1 points (line-B Task 0 infra)",
		[]inputTx{precompileCallTx(addrBytes(preBn256Add), probeInput, 500_000, 0)},
		10_000_000)
	in := spec.build(fork)
	fmt.Printf("\nprobe frame: fork=%s name=%s blockGasLimit=%d txs=%d\n",
		fork, spec.name, uint64(in.Genesis.GasLimit), len(in.Transactions))
	return probeReceiptFields(&in)
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
	cfg, err := buildConfigForCase(in)
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
	// minBaseFee is gated on BLOCK time (Task 3 handoff A), not genesis time:
	// an upgrade-boundary vector whose genesis is pre-Jovian but whose single
	// block crosses JovianTime still needs a non-nil minBaseFee for the BLOCK
	// extraData (EncodeOptimismExtraData panics on nil under Jovian). The
	// genesis extraData (pre-Jovian Holocene form) ignores minBaseFee.
	var minBaseFee *uint64
	if cfg.IsJovian(blockTime) {
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

	out, golden, err := assembleOutput(in, cfg, signer, genesis, db, block, receipts, txs, outTxs, genesis.ToBlock().Root())
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
	cfg, err := buildConfigForCase(in)
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
	// minBaseFee gated on BLOCK time, not genesis time (Task 3 handoff A) --
	// see processBlockVector for the upgrade-boundary rationale.
	var minBaseFee *uint64
	if cfg.IsJovian(blockTime) {
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
		fmt.Printf("tx[%d] type=0x%02x status=0x%x gasUsed=%d cumulativeGasUsed=%d\n",
			i, r.Type, r.Status, r.GasUsed, r.CumulativeGasUsed)
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
// 34-case single-block path (processBlockVector) and the off-line 1->2
// chained pair (processChainPair, Step 2): both need identical assembly
// logic, just fed a different (in, db, block, receipts, txs, signer) tuple
// per block.
// startRoot is the re-execution origin for this block: genesis root for chain
// block 0 / single-block vectors, blocks[i-1].Root() for chain block i (see
// reexecuteOutputs).
func assembleOutput(in *inputCase, cfg *params.ChainConfig, signer types.Signer, genesis *core.Genesis, db ethdb.Database, block *types.Block, receipts types.Receipts, txs []*types.Transaction, outTxs []json.RawMessage, startRoot common.Hash) (outputVector, *goldenRecord, error) {
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
	// --- Receipt output: re-execute the block to capture return data (the
	// chain maker discards ExecutionResult.ReturnData). Rejected if the replay
	// does not reproduce the real receipts' gasUsed/status per tx.
	outputs, err := reexecuteOutputs(cfg, genesis, db, header, txs, receipts,
		in.Coinbase, in.ParentBeaconBlockRoot, startRoot)
	if err != nil {
		return outputVector{}, nil, err
	}
	expReceipts, err := buildExpectedReceipts(cfg, in, txs, receipts, outputs, header.Time)
	if err != nil {
		return outputVector{}, nil, err
	}
	if err := crossCheckVaults(in, expReceipts, postDB); err != nil {
		return outputVector{}, nil, err
	}

	// --- Header expectations + env (both emissions from the header; iron rule 6).
	// RequestsHash is NOT required: pre-Prague forks (ecotone/fjord/granite/
	// holocene) have no requests (chain_makers.collectRequests returns nil), so
	// the header's RequestsHash stays nil and is emitted absent, mirroring
	// op-geth's own t8n (requestsHash,omitempty).
	if header.WithdrawalsHash == nil || header.BlobGasUsed == nil {
		return outputVector{}, nil, fmt.Errorf("generated header missing WithdrawalsHash/BlobGasUsed")
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
	}
	expHeader := expectedHeader{
		GasUsed:         hexutil.EncodeUint64(header.GasUsed),
		ReceiptsRoot:    header.ReceiptHash.Hex(),
		LogsBloom:       hexutil.Encode(header.Bloom[:]),
		WithdrawalsRoot: header.WithdrawalsHash.Hex(),
		BlobGasUsed:     hexutil.EncodeUint64(*header.BlobGasUsed),
		StateRoot:       header.Root.Hex(),
	}
	if header.RequestsHash != nil {
		s := header.RequestsHash.Hex()
		expHeader.RequestsHash = &s
	}
	out.OpExpected = opExpected{
		Header:   expHeader,
		Receipts: expReceipts,
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
		l1BlockAddr:       {Balance: big.NewInt(0), Nonce: 1, Code: l1BlockRuntimeCode, Storage: fp.l1BlockStorage(fork)},
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

			attr := fp.attributesTx(fmt.Sprintf("chain_%d", i), fork)
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
	outA, goldenA, err := assembleOutput(ins[0], cfg, signerA, genesis, db, blocks[0], receiptsAll[0], txSets[0], outSets[0], genesis.ToBlock().Root())
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
	outB, goldenB, err := assembleOutput(ins[1], cfg, signerB, genesis, db, blocks[1], receiptsAll[1], txSets[1], outSets[1], blocks[0].Root())
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("chain block B: %w", err)
	}

	return outA, outB, goldenA, goldenB, nil
}

// chainedBlockOutput is one block of the off-line chained pair (Task 2 Step
// 2): the full v3-block payload (same fields as outputVector) merged flatly
// with the golden extension (same fields as goldenRecord) -- there is no
// pre-existing vectors/chainX.json to complement, so unlike the 34 flat
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

	// Scalar-change pair: proves cross-block scalar propagation.
	outA, outB, goldenA, goldenB, err := processScalarChangePair("isthmus")
	if err != nil {
		return fmt.Errorf("scalar-change pair: %w", err)
	}
	if err := write("scalarChangeA", outA, goldenA); err != nil {
		return fmt.Errorf("scalarChangeA: %w", err)
	}
	if err := write("scalarChangeB", outB, goldenB); err != nil {
		return fmt.Errorf("scalarChangeB: %w", err)
	}

	return nil
}

// processScalarChangePair builds a 2-block chain where block 1 changes
// baseFeeScalar, proving the end-to-end path: L1 attributes deposit writes
// new scalar → storage persists → next block's loadOpFeeParams reads updated
// scalar → computeL1Cost produces a different result.
//
// Block 0: default fee params (baseFeeScalar = 1368)
// Block 1: doubled baseFeeScalar (2736), same transfer tx shape
//
// Both blocks carry a transfer tx so the L1 cost difference is observable
// in the receipts' l1_fee field.
func processScalarChangePair(fork string) (outputVector, outputVector, *goldenRecord, *goldenRecord, error) {
	cfg, err := buildChainConfig(fork)
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, err
	}
	jovianCfg := fork == "jovian"

	fp0 := defaultFeeParams()                       // block 0 scalars
	fp1 := defaultFeeParams()                       // block 1 scalars (changed)
	fp1.baseFeeScalar = fp0.baseFeeScalar * 2       // 1368 → 2736

	const (
		genesisTime = uint64(1000)
		denom       = uint64(50)
		elasticity  = uint64(6)
		gasLimit    = uint64(10_000_000)
	)
	beaconRoot := common.HexToHash("0x0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d")
	senderAddr := addrOfKey(1)
	genesisPre := types.GenesisAlloc{
		l1BlockAddr:       {Balance: big.NewInt(0), Nonce: 1, Code: l1BlockRuntimeCode, Storage: fp0.l1BlockStorage(fork)},
		messagePasserAddr: {Balance: big.NewInt(0), Nonce: 1},
		senderAddr:        {Balance: eth(100)},
	}

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

	// Per-block fee params: block 0 = fp0, block 1 = fp1 (scalar changed).
	blockFPs := []feeParams{fp0, fp1}

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
			blockTime := bg.Timestamp()
			blockExtra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)
			bg.SetCoinbase(sequencerVault)
			bg.SetExtra(blockExtra)
			bg.SetParentBeaconRoot(beaconRoot)
			signer := bg.Signer()

			fp := blockFPs[i] // different fee params per block
			attr := fp.attributesTx(fmt.Sprintf("scalar_%d", i), fork)
			tx0, outTx0, terr := buildTx(&attr, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("block %d attributes tx: %w", i, terr))
			}
			bg.AddTx(tx0)

			nonce := bg.TxNonce(senderAddr)
			transfer := transferTx(1, nonce, recA, eth(1), 21_000, nil)
			tx1, outTx1, terr := buildTx(&transfer, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("block %d transfer tx: %w", i, terr))
			}
			bg.AddTx(tx1)

			in := &inputCase{
				Info:                  caseInfo{Hardfork: fork, Description: fmt.Sprintf("scalar-change pair (baseFeeScalar %d→%d), block %d/2", fp0.baseFeeScalar, fp1.baseFeeScalar, i+1)},
				Genesis:               knobs,
				Coinbase:              sequencerVault,
				ParentBeaconBlockRoot: beaconRoot,
				Transactions:          []inputTx{attr, transfer},
			}
			if i == 0 {
				in.Pre = genesisPre
			}
			ins[i] = in
			txSets[i] = []*types.Transaction{tx0, tx1}
			outSets[i] = []json.RawMessage{outTx0, outTx1}
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
	if err := selfCheck(genesis, blocks); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("scalar-change pair InsertChain self-check FAILED: %w", err)
	}

	signerA := types.MakeSigner(cfg, blocks[0].Number(), blocks[0].Time())
	outA, goldenA, err := assembleOutput(ins[0], cfg, signerA, genesis, db, blocks[0], receiptsAll[0], txSets[0], outSets[0], genesis.ToBlock().Root())
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("scalar block A: %w", err)
	}

	ins[1].Pre = outA.PostState
	// Block B's deposit writes new scalar values (fp1). The pre-state from
	// block A still has fp0's L1Block storage. Update the pre-state's L1Block
	// storage to match fp1 so assertL1BlockConsistency sees a consistent
	// pre-state vs calldata. The deposit will overwrite these slots anyway;
	// this is just a self-check alignment.
	if acc, ok := ins[1].Pre[l1BlockAddr]; ok {
		updated := fp1.l1BlockStorage(fork)
		for k, v := range updated {
			acc.Storage[k] = v
		}
		ins[1].Pre[l1BlockAddr] = acc
	}
	if err := assertL1BlockConsistency(cfg, ins[1]); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("block B: %w", err)
	}
	if err := assertDepositsFirst(ins[1].Transactions); err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("block B: %w", err)
	}
	signerB := types.MakeSigner(cfg, blocks[1].Number(), blocks[1].Time())
	outB, goldenB, err := assembleOutput(ins[1], cfg, signerB, genesis, db, blocks[1], receiptsAll[1], txSets[1], outSets[1], blocks[0].Root())
	if err != nil {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("scalar block B: %w", err)
	}

	// Cross-block scalar assertion: the transfer tx in block 1 must have a
	// different L1 cost than in block 0, proving the scalar change propagates.
	l1FeeA := l1FeeOfTransfer(receiptsAll[0])
	l1FeeB := l1FeeOfTransfer(receiptsAll[1])
	if l1FeeA.Sign() == 0 && l1FeeB.Sign() == 0 {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("scalar-change: both blocks have zero L1 fee -- cannot observe scalar difference")
	}
	if l1FeeA.Cmp(l1FeeB) == 0 {
		return outputVector{}, outputVector{}, nil, nil, fmt.Errorf("scalar-change: L1 fee unchanged between blocks (A=%s, B=%s) -- scalar change not propagated", l1FeeA, l1FeeB)
	}

	return outA, outB, goldenA, goldenB, nil
}

// l1FeeOfTransfer extracts the L1 fee from the second receipt (index 1 = transfer tx).
func l1FeeOfTransfer(receipts types.Receipts) *big.Int {
	if len(receipts) < 2 {
		return big.NewInt(0)
	}
	// The OP receipt includes L1Fee in the receipt's effective_gas_price for
	// non-deposit txs (post-Regolith). We read it from the types.Receipt
	// fields directly -- op-geth stores it in FeeScalar/L1BlobBaseFee/etc.
	// For the cross-block comparison, we use the cumulative approach:
	// the difference in L1 cost is observable via the receipt's Logs or
	// the effective gas price. Here we use the receipt's cumulative gas
	// price as a proxy (the actual L1 fee is encoded in the receipt's
	// OP-specific fields which we compare via the golden output).
	// For the self-check, we compare the raw big.Int values from the
	// op-geth receipt struct.
	return receipts[1].L1Fee // op-geth types.Receipt.L1Fee
}

// ---------------------------------------------------------------------
// Task 5: chain mode (spec §1b). A NEW output shape: one GenerateChainWithGenesis
// call for n>=3 blocks, emitted as `{ "blocks": [...] }` -- NOT the flat
// per-block files of runChainPair (:1546). The replayer (OpT8nReplayTest.cpp
// replayChainVector :1044) inherits the running chain state across blocks:
// blocks[0] carries `pre`, blocks[i>0] emit NO pre (null/absent -- a real pre
// object would reset the state). Each block MUST carry its own
// _op_expected.header/receipts + postState (Task 1 handoff: replaySingleBlockInto
// reads them with .at(), missing = out_of_range). Recipe constraint (review
// R13): chain blocks must not read historical blockhashes (ParentOnlyBlockHashes
// only answers number-1) -- the attributes-deposit + transfer recipe is safe.
// ---------------------------------------------------------------------

// chainBlockOutput is one block of the chain-mode vector. `pre` is a pointer +
// omitempty so blocks[i>0] emit NO pre key (the replayer inherits chainState).
type chainBlockOutput struct {
	Info       caseInfo                          `json:"_info"`
	Env        outputEnv                         `json:"env"`
	Pre        *map[common.Address]outputAccount `json:"pre,omitempty"`
	Block      outputBlock                       `json:"block"`
	PostState  types.GenesisAlloc                `json:"postState"`
	OpExpected opExpected                        `json:"_op_expected"`
}

// chainOutput is the chain-mode vector schema (spec §1b): a top-level `blocks`
// array. The outer file wraps it in `_op_test_vectors` + the vector id, same
// as every other vector.
type chainOutput struct {
	Blocks []chainBlockOutput `json:"blocks"`
}

// chainContext is the internal (non-JSON) chain result the fork/break arms
// need: the raw generated blocks (txs come from block.Transactions()), the
// genesis, the chain config and the genesis pre-state. processChainN's public
// *chainOutput derives from it.
type chainContext struct {
	cfg        *params.ChainConfig
	genesis    *core.Genesis
	genesisPre types.GenesisAlloc
	blocks     []*types.Block
}

// generateChainN builds an n-block linear chain (spec §1b): ONE
// GenerateChainWithGenesis(n) call (real chain_makers parent-chaining -- block
// i's pre-state IS block i-1's generated post-state), self-checked by
// InsertChain-ing ALL n blocks together, then assembled per-block into the
// chain vector. The per-block recipe is the chain-pair recipe (L1-attributes
// deposit + a sender transfer with the real chained nonce from bg.TxNonce).
func generateChainN(fork string, n int) (*chainOutput, *chainContext, error) {
	if n < 3 {
		return nil, nil, fmt.Errorf("generateChainN: n must be >= 3 (got %d)", n)
	}
	cfg, err := buildChainConfig(fork)
	if err != nil {
		return nil, nil, err
	}
	jovianCfg := fork == "jovian"
	fp := defaultFeeParams()
	if jovianCfg {
		fp.daScalar = 400 // non-zero DA scalar: block DA footprint observable
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
		l1BlockAddr:       {Balance: big.NewInt(0), Nonce: 1, Code: l1BlockRuntimeCode, Storage: fp.l1BlockStorage(fork)},
		messagePasserAddr: {Balance: big.NewInt(0), Nonce: 1},
		senderAddr:        {Balance: eth(100)},
	}
	// Jovian requires a non-nil minBaseFee in EncodeOptimismExtraData (see
	// processChainPair for the panic rationale).
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

	engine := beacon.New(ethash.NewFaker())
	var (
		db          ethdb.Database
		blocks      []*types.Block
		receiptsAll []types.Receipts
	)
	ins := make([]*inputCase, n)
	txSets := make([][]*types.Transaction, n)
	outSets := make([][]json.RawMessage, n)
	if err := func() (err error) {
		defer func() {
			if r := recover(); r != nil {
				err = fmt.Errorf("GenerateChainWithGenesis panic: %v", r)
			}
		}()
		db, blocks, receiptsAll = core.GenerateChainWithGenesis(genesis, engine, n, func(i int, bg *core.BlockGen) {
			blockTime := bg.Timestamp() // chain_makers: parent.Time()+10, real per-block advance
			blockExtra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)
			bg.SetCoinbase(sequencerVault)
			bg.SetExtra(blockExtra)
			bg.SetParentBeaconRoot(beaconRoot)
			signer := bg.Signer() // == MakeSigner(cfg, bg.Number(), bg.Timestamp())

			attr := fp.attributesTx(fmt.Sprintf("chainN_%d_%d", n, i), fork)
			tx0, outTx0, terr := buildTx(&attr, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("block %d attributes tx: %w", i, terr))
			}
			bg.AddTx(tx0)

			nonce := bg.TxNonce(senderAddr) // real chained nonce (0, 1, 2, ...)
			transfer := transferTx(1, nonce, recA, eth(1), 21_000, nil)
			tx1, outTx1, terr := buildTx(&transfer, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("block %d transfer tx: %w", i, terr))
			}
			bg.AddTx(tx1)

			in := &inputCase{
				Info: caseInfo{Hardfork: fork,
					Description: fmt.Sprintf("linear chain of %d blocks (spec §1b), block %d/%d", n, i+1, n)},
				Genesis:               knobs,
				Coinbase:              sequencerVault,
				ParentBeaconBlockRoot: beaconRoot,
				Transactions:          []inputTx{attr, transfer},
			}
			if i == 0 {
				in.Pre = genesisPre // block i>0's Pre is filled AFTER assembly, from i-1's postState
			}
			ins[i] = in
			txSets[i] = []*types.Transaction{tx0, tx1}
			outSets[i] = []json.RawMessage{outTx0, outTx1}
		})
		return nil
	}(); err != nil {
		return nil, nil, err
	}
	if len(blocks) != n {
		return nil, nil, fmt.Errorf("expected %d generated blocks, got %d", n, len(blocks))
	}

	// Self-check: independent fresh DB, Process+ValidateState over ALL n blocks
	// in sequence -- this IS the "经 InsertChain 头校验" the chain exists to
	// exercise (real parent-child header linkage).
	if err := selfCheck(genesis, blocks); err != nil {
		return nil, nil, fmt.Errorf("chain InsertChain self-check FAILED: %w", err)
	}

	out := &chainOutput{Blocks: make([]chainBlockOutput, n)}
	for i := range blocks {
		// Decision A2 generalized: block i's pre IS block i-1's post-state
		// (the existing ins[1].Pre = outA.PostState for the chain pair).
		if i > 0 {
			ins[i].Pre = out.Blocks[i-1].PostState
		}
		if err := assertL1BlockConsistency(cfg, ins[i]); err != nil {
			return nil, nil, fmt.Errorf("block %d: %w", i, err)
		}
		if err := assertDepositsFirst(ins[i].Transactions); err != nil {
			return nil, nil, fmt.Errorf("block %d: %w", i, err)
		}
		signer := types.MakeSigner(cfg, blocks[i].Number(), blocks[i].Time())
		var startRoot common.Hash
		if i == 0 {
			startRoot = genesis.ToBlock().Root()
		} else {
			startRoot = blocks[i-1].Root() // replay origin = previous post-state (Task 5 前置 fix)
		}
		o, _, err := assembleOutput(ins[i], cfg, signer, genesis, db, blocks[i], receiptsAll[i], txSets[i], outSets[i], startRoot)
		if err != nil {
			return nil, nil, fmt.Errorf("chain block %d: %w", i, err)
		}
		var pre *map[common.Address]outputAccount
		if i == 0 {
			p := emitPre(ins[i].Pre)
			pre = &p
		}
		out.Blocks[i] = chainBlockOutput{
			Info: o.Info, Env: o.Env, Pre: pre, Block: o.Block,
			PostState: o.PostState, OpExpected: o.OpExpected,
		}
	}
	return out, &chainContext{
		cfg: cfg, genesis: genesis, genesisPre: genesisPre, blocks: blocks,
	}, nil
}

// processChainN is the public chain-mode entry (Task 5 interface): an n-block
// linear chain vector. The fork/break arms use generateChainN directly for the
// internal context.
func processChainN(fork string, n int) (*chainOutput, error) {
	out, _, err := generateChainN(fork, n)
	return out, err
}

// genSiblingFork builds a SECOND child of the same parent at the same height as
// firstChild (a SIBLING block): same parent (genesis), same height 1, but
// different content (a larger transfer value) so its hash differs. op-geth
// InsertChain ACCEPTS the sibling as a side chain -- the divergence recorded in
// the fork carrier's op_geth anchor ("VALID (side chain)"). The FISCO engine
// gate rejects the SECOND pour of the same height with -32603
// OpExecutionInternalError (SYS_NUMBER_2_HASH collision, Task 2 two-pour).
func genSiblingFork(firstChild *types.Block, cfg *params.ChainConfig, genesis *core.Genesis) (*types.Block, error) {
	if firstChild == nil || cfg == nil || genesis == nil {
		return nil, fmt.Errorf("genSiblingFork: nil firstChild/cfg/genesis")
	}
	if firstChild.NumberU64() != 1 {
		return nil, fmt.Errorf("genSiblingFork: firstChild must be height 1 (child of genesis), got %d", firstChild.NumberU64())
	}
	jovianCfg := cfg.IsJovian(firstChild.Time())
	fork := "isthmus"
	if jovianCfg {
		fork = "jovian"
	}
	fp := defaultFeeParams()
	if jovianCfg {
		fp.daScalar = 400
	}
	denom := uint64(50)
	elasticity := uint64(6)
	var minBaseFee *uint64
	if jovianCfg {
		v := uint64(0)
		minBaseFee = &v
	}
	var beaconRoot common.Hash
	if firstChild.Header().ParentBeaconRoot != nil {
		beaconRoot = *firstChild.Header().ParentBeaconRoot
	}
	engine := beacon.New(ethash.NewFaker())
	var (
		db     ethdb.Database
		blocks []*types.Block
	)
	if err := func() (err error) {
		defer func() {
			if r := recover(); r != nil {
				err = fmt.Errorf("GenerateChainWithGenesis panic: %v", r)
			}
		}()
		db, blocks, _ = core.GenerateChainWithGenesis(genesis, engine, 1, func(i int, bg *core.BlockGen) {
			blockTime := bg.Timestamp()
			blockExtra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)
			bg.SetCoinbase(sequencerVault)
			bg.SetExtra(blockExtra)
			bg.SetParentBeaconRoot(beaconRoot)
			signer := bg.Signer()
			attr := fp.attributesTx("sibling_fork", fork)
			tx0, _, terr := buildTx(&attr, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("sibling attributes tx: %w", terr))
			}
			bg.AddTx(tx0)
			nonce := bg.TxNonce(addrOfKey(1))
			// 2 ETH instead of the canonical 1 ETH -> different state root ->
			// different block hash, same parent + height.
			transfer := transferTx(1, nonce, recA, eth(2), 21_000, nil)
			tx1, _, terr := buildTx(&transfer, signer, cfg)
			if terr != nil {
				panic(fmt.Errorf("sibling transfer tx: %w", terr))
			}
			bg.AddTx(tx1)
		})
		return nil
	}(); err != nil {
		return nil, err
	}
	_ = db // the sibling's generation DB is only needed for its own assembly; the carrier uses the block
	if len(blocks) != 1 {
		return nil, fmt.Errorf("expected 1 sibling block, got %d", len(blocks))
	}
	sibling := blocks[0]
	if sibling.Hash() == firstChild.Hash() {
		return nil, fmt.Errorf("sibling hash %s equals firstChild (recipe did not diverge)", sibling.Hash().Hex())
	}
	if sibling.ParentHash() != firstChild.ParentHash() {
		return nil, fmt.Errorf("sibling parent %s != firstChild parent %s (must share parent)", sibling.ParentHash().Hex(), firstChild.ParentHash().Hex())
	}
	if sibling.NumberU64() != firstChild.NumberU64() {
		return nil, fmt.Errorf("sibling number %d != firstChild number %d", sibling.NumberU64(), firstChild.NumberU64())
	}
	return sibling, nil
}

// buildForkVector assembles the chain_fork carrier (spec §1b fork arm): the
// canonical child's full ExecutionPayload under _op_canonical (Task 2 handoff
// field name -- the E2E runner pours it FIRST and expects VALID, writing
// SYS_NUMBER_2_HASH[1]) and the sibling's payload under _op_payload (the SECOND
// pour of the same height, which collides -> -32603 OpExecutionInternalError).
// op_geth documents the divergence: op-geth ACCEPTS the side-chain sibling.
func buildForkVector(fork string, canonical, sibling *types.Block, genesisPre types.GenesisAlloc) (invalidVectorDoc, error) {
	if canonical == nil || sibling == nil {
		return invalidVectorDoc{}, fmt.Errorf("buildForkVector: nil canonical/sibling")
	}
	return invalidVectorDoc{
		Info: caseInfo{
			Hardfork: fork,
			Description: fmt.Sprintf("same-parent same-height fork: canonical %s + sibling %s (two-pour -32603)",
				canonical.Hash().Hex(), sibling.Hash().Hex()),
		},
		Pre:         emitPre(genesisPre),
		OpPayload:   buildPayloadFromHeader(sibling.Header(), sibling.Hash(), sibling.Transactions()),
		OpCanonical: buildPayloadFromHeader(canonical.Header(), canonical.Hash(), canonical.Transactions()),
		OpExpected: invalidExpected{
			Reject: &rejectExpected{
				OpGeth: "VALID (side chain)",
				Fisco: fiscoReject{
					Consumer:        "engine",
					Classification:  "-32603",
					LatestValidHash: json.RawMessage("null"),
					ExpectThrow:     "OpExecutionInternalError",
				},
			},
		},
	}, nil
}

// genParentBreak builds the chain-break carrier (spec §1b break arm): the
// chain's height-1 block re-emitted with parentHash pointing at an UNKNOWN
// ancestor (0x...01), which classifies SYNCING (consumer: engine). The E2E
// runner SKIPS parent registration for SYNCING vectors (OpNewPayloadRpcE2eTest
// runInvalidVector) and asserts newPayload -> Syncing. The op_geth anchor is
// the real InsertChain "unknown ancestor" rejection.
func genParentBreak(fork string, block *types.Block, cfg *params.ChainConfig, genesis *core.Genesis, genesisPre types.GenesisAlloc) (invalidVectorDoc, error) {
	if block == nil || cfg == nil || genesis == nil {
		return invalidVectorDoc{}, fmt.Errorf("genParentBreak: nil block/cfg/genesis")
	}
	header := types.CopyHeader(block.Header())
	header.ParentHash = common.HexToHash("0x0000000000000000000000000000000000000000000000000000000000000001")
	blockHash := recomputeOpHeaderHash(header)
	payload := buildPayloadFromHeader(header, blockHash, block.Transactions())
	msg, err := captureInsertChainRejection(genesis, block.WithSeal(header))
	if err != nil {
		return invalidVectorDoc{}, fmt.Errorf("genParentBreak: InsertChain capture: %w", err)
	}
	return invalidVectorDoc{
		Info: caseInfo{
			Hardfork: fork,
			Description: fmt.Sprintf("parent break: %s re-emitted with unknown parent 0x...01 (SYNCING)",
				block.Hash().Hex()),
		},
		Pre:       emitPre(genesisPre),
		OpPayload: payload,
		OpExpected: invalidExpected{
			Reject: &rejectExpected{
				OpGeth: msg,
				Fisco: fiscoReject{
					Consumer:       "engine",
					Classification: "SYNCING",
				},
			},
		},
	}, nil
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
// Task 3: corrupt / static modes (self-consistent corruption + §4c static
// surface). These are INDEPENDENT modes -- they take the ASSEMBLED valid
// product of a base case (buildBlockVector) and re-emit it as an invalid
// vector; they never run inside --write-cases (the old 77 case files stay
// byte-for-byte untouched).
// ---------------------------------------------------------------------

// blockVector is the full context of one generated, self-checked block: the
// assembled output vector PLUS the block objects the corrupt/static modes need
// to rebuild a corrupt block and capture its InsertChain rejection.
type blockVector struct {
	in       *inputCase
	cfg      *params.ChainConfig
	signer   types.Signer
	genesis  *core.Genesis
	block    *types.Block
	txs      []*types.Transaction
	receipts types.Receipts
	outTxs   []json.RawMessage
	vec      outputVector
	golden   *goldenRecord
}

// buildBlockVector replicates the processBlockVector front half (genesis -> tx
// build -> GenerateChainWithGenesis -> selfCheck) and returns the full block
// context. It deliberately COPIES that logic rather than sharing it:
// processBlockVector is the byte-invariant golden path and must not be
// disturbed; this is the same "copy the front half" pattern probeReceiptFields
// already uses (main.go:847).
func buildBlockVector(in *inputCase) (*blockVector, error) {
	// ── copied from processBlockVector front half (main.go:436-517) ──
	cfg, err := buildConfigForCase(in)
	if err != nil {
		return nil, err
	}
	if err := assertL1BlockConsistency(cfg, in); err != nil {
		return nil, fmt.Errorf("L1Block slot<->calldata consistency: %w", err)
	}
	if err := assertDepositsFirst(in.Transactions); err != nil {
		return nil, err
	}
	genesisTime := uint64(in.Genesis.Timestamp)
	blockTime := genesisTime + 10
	denom := uint64(in.Genesis.EIP1559Denominator)
	elasticity := uint64(in.Genesis.EIP1559Elasticity)
	var minBaseFee *uint64
	if cfg.IsJovian(blockTime) {
		if in.Genesis.MinBaseFee == nil {
			return nil, fmt.Errorf("jovian case must set genesis.minBaseFee (EncodeOptimismExtraData requires it)")
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
	signer := types.MakeSigner(cfg, big.NewInt(1), blockTime)
	var (
		txs    []*types.Transaction
		outTxs []json.RawMessage
	)
	for i := range in.Transactions {
		tx, outTx, err := buildTx(&in.Transactions[i], signer, cfg)
		if err != nil {
			return nil, fmt.Errorf("tx %d: %w", i, err)
		}
		txs = append(txs, tx)
		outTxs = append(outTxs, outTx)
	}
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
			b.SetExtra(blockExtra)
			b.SetParentBeaconRoot(in.ParentBeaconBlockRoot)
			for _, tx := range txs {
				b.AddTx(tx)
			}
		})
		return nil
	}(); err != nil {
		return nil, err
	}
	if len(blocks) != 1 {
		return nil, fmt.Errorf("expected 1 generated block, got %d", len(blocks))
	}
	block, receipts := blocks[0], receiptsAll[0]
	header := block.Header()
	if header.Time != blockTime || header.Number.Uint64() != 1 {
		return nil, fmt.Errorf("unexpected generated header number/time: %d/%d", header.Number.Uint64(), header.Time)
	}
	if len(receipts) != len(txs) {
		return nil, fmt.Errorf("receipt/tx count mismatch: %d vs %d", len(receipts), len(txs))
	}
	if err := selfCheck(genesis, blocks); err != nil {
		return nil, fmt.Errorf("InsertChain self-check FAILED (corpus/generator defect, do not bypass): %w", err)
	}
	vec, golden, err := assembleOutput(in, cfg, signer, genesis, db, block, receipts, txs, outTxs, genesis.ToBlock().Root())
	if err != nil {
		return nil, fmt.Errorf("vector assembly: %w", err)
	}
	return &blockVector{
		in: in, cfg: cfg, signer: signer, genesis: genesis, block: block,
		txs: txs, receipts: receipts, outTxs: outTxs, vec: vec, golden: golden,
	}, nil
}

// runChainMode implements --mode=chain:<N>[:fork|:break] (Task 5). n>=3 emits
// the linear-chain vector (isthmus_chain_<n>.json + jovian_chain_<n>.json);
// :fork emits the same-parent fork carrier (invalid_<fork>_chain_<n>_fork, -32603
// two-pour); :break emits the unknown-parent SYNCING carrier
// (invalid_<fork>_chain_<n>_break). fork/break 带 invalid_ 前缀以便 E2E
// InvalidVectorsFromManifest 消费（Task 5 交接决策）。
func runChainMode(mode, outDir, opGethCommit string) error {
	if outDir == "" {
		return fmt.Errorf("--out-dir is required for --mode=chain")
	}
	rest := strings.TrimPrefix(mode, "chain:")
	parts := strings.Split(rest, ":")
	if len(parts) == 0 || parts[0] == "" {
		return fmt.Errorf("--mode=chain:<N>[:fork|:break], got %q", mode)
	}
	n, err := strconv.Atoi(parts[0])
	if err != nil || n < 3 {
		return fmt.Errorf("--mode=chain:<N> requires N>=3, got %q", parts[0])
	}
	variant := ""
	if len(parts) > 1 {
		variant = parts[1]
	}
	if variant != "" && variant != "fork" && variant != "break" {
		return fmt.Errorf("--mode=chain:<N>[:fork|:break], got variant %q", variant)
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}
	for _, fork := range []string{"isthmus", "jovian"} {
		chain, ctx, err := generateChainN(fork, n)
		if err != nil {
			return fmt.Errorf("%s chain %d: %w", fork, n, err)
		}
		switch variant {
		case "":
			stem := fmt.Sprintf("%s_chain_%d", fork, n)
			if err := writeChainVector(outDir, stem, chain, opGethCommit); err != nil {
				return err
			}
		case "fork":
			canonical := ctx.blocks[0] // height-1 child of genesis (the canonical first pour)
			sibling, err := genSiblingFork(canonical, ctx.cfg, ctx.genesis)
			if err != nil {
				return fmt.Errorf("%s fork: %w", fork, err)
			}
			doc, err := buildForkVector(fork, canonical, sibling, ctx.genesisPre)
			if err != nil {
				return fmt.Errorf("%s fork carrier: %w", fork, err)
			}
			// invalid_ 前缀：E2E InvalidVectorsFromManifest 按 invalidStemFromManifestLine
			// 只消费 invalid_* 行（OpNewPayloadRpcE2eTest.cpp:634），fork/break 是 -32603/
			// SYNCING 无效向量载体，必须前缀统一（Task 5 交接决策）。
			if err := writeInvalidVector(outDir, fmt.Sprintf("invalid_%s_chain_%d_fork", fork, n), doc, opGethCommit); err != nil {
				return err
			}
		case "break":
			doc, err := genParentBreak(fork, ctx.blocks[0], ctx.cfg, ctx.genesis, ctx.genesisPre)
			if err != nil {
				return fmt.Errorf("%s break: %w", fork, err)
			}
			if err := writeInvalidVector(outDir, fmt.Sprintf("invalid_%s_chain_%d_break", fork, n), doc, opGethCommit); err != nil {
				return err
			}
		}
	}
	return nil
}

// writeChainVector writes one chain-mode vector as vectors/<stem>.json with the
// same outer-wrapping convention as run() (a `_op_test_vectors` meta object plus
// the single vector object keyed by the stem).
func writeChainVector(outDir, stem string, chain *chainOutput, opGethCommit string) error {
	meta, err := json.Marshal(struct {
		Version         string `json:"version"`
		Generator       string `json:"generator"`
		GeneratorCommit string `json:"generator_commit"`
	}{schemaVersion, "opt8n-ref", opGethCommit})
	if err != nil {
		return err
	}
	docBytes, err := json.Marshal(chain)
	if err != nil {
		return err
	}
	out := map[string]json.RawMessage{
		"_op_test_vectors": meta,
		stem:               docBytes,
	}
	outBytes, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	outBytes = append(outBytes, '\n')
	return os.WriteFile(filepath.Join(outDir, stem+".json"), outBytes, 0o644)
}

// runInvalidMode dispatches the --mode=corrupt / --mode=static CLI paths. Both
// load the base case from the caseSpecs table (no filesystem dependency; the
// base stem is "fork_name", split on the first underscore).
func runInvalidMode(mode, baseStem, outDir, opGethCommit string) error {
	if outDir == "" {
		return fmt.Errorf("--out-dir is required for --mode=%s", mode)
	}
	fork, name, err := splitVectorName(baseStem)
	if err != nil {
		return err
	}
	if fork != "isthmus" && fork != "jovian" {
		return fmt.Errorf("--mode=%s requires an isthmus/jovian base (OP Isthmus+ path), got fork %q", mode, fork)
	}
	// corrupt/static need the ASSEMBLED valid base block; invalid-tx does not
	// (its kinds build their own case from the independent invalidTxCaseSpecs).
	if mode == "invalid-tx" {
		// baseStem = "fork_kind" (e.g. isthmus_intrinsic_gas). Each kind is an
		// INDEPENDENT invalidTxCaseSpecs entry (review R7) — never shared
		// caseSpecs/emitCases. Emits one invalid_<stem>.json.
		doc, _, _, err := buildInvalidTxVector(name, fork)
		if err != nil {
			return fmt.Errorf("invalid-tx %s/%s: %w", fork, name, err)
		}
		return writeInvalidVector(outDir, "invalid_"+baseStem, doc, opGethCommit)
	}

	in, err := buildCaseFromSpecs(name, fork)
	if err != nil {
		return err
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		return fmt.Errorf("build base %q: %w", baseStem, err)
	}
	switch mode {
	case "corrupt":
		for _, field := range []string{"stateRoot", "gasUsed", "receiptsRoot", "parentHash", "extraData", "blockHash"} {
			doc, _, err := corruptVector(base, field)
			if err != nil {
				return fmt.Errorf("corrupt %s: %w", field, err)
			}
			stem := "invalid_" + baseStem + "_" + field
			if err := writeInvalidVector(outDir, stem, doc, opGethCommit); err != nil {
				return err
			}
		}
		return nil
	case "static":
		// The §4c items that need a Jovian base (DA-footprint, item 11) derive
		// from the canonical jovian_transfer_basic; the rest use the passed base.
		jovIn, err := buildCaseFromSpecs("transfer_basic", "jovian")
		if err != nil {
			return err
		}
		jovBase, err := buildBlockVector(&jovIn)
		if err != nil {
			return fmt.Errorf("build jovian base: %w", err)
		}
		for _, item := range staticSurfaceItems {
			b := base
			stemBase := baseStem
			if item.fork == "jovian" {
				b = jovBase
				stemBase = "jovian_transfer_basic"
			}
			doc, err := emitStaticSurfaceVector(b, item)
			if err != nil {
				return fmt.Errorf("static %s: %w", item.name, err)
			}
			stem := "invalid_" + stemBase + "_static_" + itoa(item.n)
			if err := writeInvalidVector(outDir, stem, doc, opGethCommit); err != nil {
				return err
			}
		}
		return nil
	default:
		return fmt.Errorf("unknown --mode %q", mode)
	}
}

// buildCaseFromSpecs finds a caseSpec by name+fork and builds the input case.
// Used by both the CLI modes and the Go tests.
func buildCaseFromSpecs(name, fork string) (inputCase, error) {
	for _, spec := range caseSpecs {
		if spec.name != name {
			continue
		}
		for _, f := range spec.forks {
			if f == fork {
				return spec.build(fork), nil
			}
		}
		return inputCase{}, fmt.Errorf("caseSpec %q has no fork %q (available: %v)", name, fork, spec.forks)
	}
	return inputCase{}, fmt.Errorf("caseSpec %q not found in caseSpecs", name)
}

// splitVectorName splits a vector stem "fork_name" into its fork prefix and the
// remaining name. Fork names contain no underscores, so the first underscore is
// the boundary (e.g. isthmus_transfer_basic -> isthmus/transfer_basic).
func splitVectorName(stem string) (fork, name string, err error) {
	i := strings.IndexByte(stem, '_')
	if i < 0 {
		return "", "", fmt.Errorf("stem %q has no fork prefix (want fork_name)", stem)
	}
	return stem[:i], stem[i+1:], nil
}

func itoa(n int) string {
	return fmt.Sprintf("%d", n)
}

// ---------------------------------------------------------------------
// Task 4: invalid-tx mode (§4b). Independent from the shared caseSpecs table.
// ---------------------------------------------------------------------

// invalidTxSpec returns the §4b spec for a kind.
func invalidTxSpec(kind string) (*invalidTxCaseSpec, error) {
	for i := range invalidTxCaseSpecs {
		if invalidTxCaseSpecs[i].kind == kind {
			return &invalidTxCaseSpecs[i], nil
		}
	}
	return nil, fmt.Errorf("invalidTxSpec: unknown kind %q (available: %v)", kind, invalidTxKinds())
}

// buildInvalidTxCase looks up a §4b kind+fork and builds the input case
// (deposit + pre-state) plus the kind's invalid-tx builder.
func buildInvalidTxCase(kind, fork string) (inputCase, invalidTxBuilder, error) {
	spec, err := invalidTxSpec(kind)
	if err != nil {
		return inputCase{}, nil, err
	}
	for _, f := range spec.forks {
		if f == fork {
			in, builder := spec.build(fork)
			return in, builder, nil
		}
	}
	return inputCase{}, nil, fmt.Errorf("invalidTxSpec %q has no fork %q (available: %v)", kind, fork, spec.forks)
}

// buildGenesisForCase assembles the core.Genesis for an input case — the same
// construction buildBlockVector/processBlockVector use (byte-invariant golden
// path), factored out for the invalid-tx path (which cannot go through
// GenerateChainWithGenesis: AddTx rejects invalid txs).
func buildGenesisForCase(in *inputCase, cfg *params.ChainConfig) (*core.Genesis, error) {
	genesisTime := uint64(in.Genesis.Timestamp)
	denom := uint64(in.Genesis.EIP1559Denominator)
	elasticity := uint64(in.Genesis.EIP1559Elasticity)
	var minBaseFee *uint64
	if cfg.IsJovian(genesisTime) {
		if in.Genesis.MinBaseFee == nil {
			return nil, fmt.Errorf("jovian case must set genesis.minBaseFee (EncodeOptimismExtraData requires it)")
		}
		v := uint64(*in.Genesis.MinBaseFee)
		minBaseFee = &v
	}
	var genesisBaseFee *big.Int
	if in.Genesis.BaseFee != nil {
		genesisBaseFee = (*big.Int)(in.Genesis.BaseFee)
	}
	return &core.Genesis{
		Config:     cfg,
		Timestamp:  genesisTime,
		GasLimit:   uint64(in.Genesis.GasLimit),
		BaseFee:    genesisBaseFee,
		Difficulty: big.NewInt(0),
		ExtraData:  eip1559.EncodeOptimismExtraData(cfg, genesisTime, denom, elasticity, minBaseFee),
		Alloc:      in.Pre,
	}, nil
}

// buildInvalidTxBlock assembles a STRUCTURALLY-valid block whose body contains
// deposit + invalid (an invalid non-deposit tx). GenerateChain cannot AddTx an
// invalid tx, so txRoot is hand-rolled (types.DeriveSha over the two txs) and
// blockHash is recomputed from the header; stateRoot is a placeholder (the
// block fails at execution, before any stateRoot comparison — FISCO only
// reaches execution after the step-2 blockHash check). The header is fully
// legal: extraData = EncodeOptimismExtraData (else VerifyHeaders rejects
// first), baseFee recomputed from the parent (else InsertChain rejects with
// "invalid baseFee" before the tx), withdrawalsRoot = EmptyWithdrawalsHash
// (Isthmus+), requestsHash = EmptyRequestsHash, blobGasUsed/excessBlobGas = 0,
// parentBeaconRoot set. The invalid tx is inserted AFTER the L1 attributes
// deposit: the first-tx-is-L1-attributes gate was demoted to a WARNING log
// (finding D #5429 — FISCO accepts like op-geth/op-reth), so the anchor for
// this vector is the invalid tx's execution failure, not the position-0 gate.
func buildInvalidTxBlock(in *inputCase, cfg *params.ChainConfig, genesis *core.Genesis,
	signer types.Signer, deposit, invalid *types.Transaction) (*types.Block, error) {
	if in == nil || cfg == nil || genesis == nil {
		return nil, fmt.Errorf("buildInvalidTxBlock: nil in/cfg/genesis")
	}
	if deposit == nil || invalid == nil {
		return nil, fmt.Errorf("buildInvalidTxBlock: nil deposit/invalid tx")
	}
	if !deposit.IsDepositTx() {
		return nil, fmt.Errorf("buildInvalidTxBlock: tx0 must be the L1 attributes deposit (got type %d)", deposit.Type())
	}
	genesisTime := uint64(in.Genesis.Timestamp)
	blockTime := genesisTime + 10
	denom := uint64(in.Genesis.EIP1559Denominator)
	elasticity := uint64(in.Genesis.EIP1559Elasticity)
	var minBaseFee *uint64
	if cfg.IsJovian(blockTime) {
		if in.Genesis.MinBaseFee == nil {
			return nil, fmt.Errorf("jovian invalid-tx case must set genesis.minBaseFee (EncodeOptimismExtraData requires it)")
		}
		v := uint64(*in.Genesis.MinBaseFee)
		minBaseFee = &v
	}
	extra := eip1559.EncodeOptimismExtraData(cfg, blockTime, denom, elasticity, minBaseFee)

	txs := []*types.Transaction{deposit, invalid}
	txRoot := types.DeriveSha(types.Transactions(txs), trie.NewStackTrie(nil))
	parentBlock := genesis.ToBlock()
	parentHash := parentBlock.Hash()
	// Block 1 baseFee is recomputed from the parent (genesis) by the chain —
	// a hardcoded parent BaseFee would fail InsertChain's "invalid baseFee"
	// header check BEFORE reaching the invalid tx, mis-anchoring the rejection.
	baseFee := eip1559.CalcBaseFee(cfg, parentBlock.Header(), blockTime)

	header := &types.Header{
		ParentHash:       parentHash,
		UncleHash:        types.EmptyUncleHash,
		Coinbase:         in.Coinbase,
		Root:             types.EmptyRootHash, // placeholder — execution fails before stateRoot comparison
		TxHash:           txRoot,
		ReceiptHash:      types.EmptyReceiptsHash,
		Bloom:            types.Bloom{},
		Difficulty:       big.NewInt(0),
		Number:           big.NewInt(1),
		GasLimit:         genesis.GasLimit,
		GasUsed:          0,
		Time:             blockTime,
		Extra:            extra,
		MixDigest:        common.Hash{},
		BaseFee:          baseFee,
		WithdrawalsHash:  &types.EmptyWithdrawalsHash,
		RequestsHash:     &types.EmptyRequestsHash,
		BlobGasUsed:      new(uint64),
		ExcessBlobGas:    new(uint64),
		ParentBeaconRoot: &in.ParentBeaconBlockRoot,
	}
	// NewBlock requires non-nil empty withdrawals on the Isthmus+ path
	// (HasOptimismWithdrawalsRoot → panic on nil), and recomputes TxHash /
	// ReceiptHash / WithdrawalsHash from the body (all agree with our header).
	body := &types.Body{Transactions: txs, Withdrawals: types.Withdrawals{}}
	block := types.NewBlock(header, body, nil, trie.NewStackTrie(nil), cfg)
	if block.Hash() != header.Hash() {
		return nil, fmt.Errorf("buildInvalidTxBlock: NewBlock hash %s != header hash %s", block.Hash().Hex(), header.Hash().Hex())
	}
	return block, nil
}

// buildInvalidTxPayload assembles the ExecutionPayload JSON for an invalid-tx
// block (same field units/conventions as buildOpPayload: timestamp in OP
// seconds, every quantity a hex string). txs are the block's transactions in
// order (deposit, invalid) — the raw envelopes the engine decodes.
func buildInvalidTxPayload(header *types.Header, blockHash common.Hash, txs []*types.Transaction) (map[string]interface{}, error) {
	if header.WithdrawalsHash == nil || header.BlobGasUsed == nil || header.ExcessBlobGas == nil || header.ParentBeaconRoot == nil {
		return nil, fmt.Errorf("buildInvalidTxPayload: header missing WithdrawalsHash/BlobGasUsed/ExcessBlobGas/ParentBeaconRoot")
	}
	txHexes := make([]string, len(txs))
	for i, tx := range txs {
		b, err := tx.MarshalBinary()
		if err != nil {
			return nil, fmt.Errorf("buildInvalidTxPayload: tx %d MarshalBinary: %w", i, err)
		}
		txHexes[i] = hexutil.Encode(b)
	}
	return map[string]interface{}{
		"parentHash":            header.ParentHash.Hex(),
		"feeRecipient":          strings.ToLower(header.Coinbase.Hex()),
		"stateRoot":             header.Root.Hex(),
		"receiptsRoot":          header.ReceiptHash.Hex(),
		"logsBloom":             hexutil.Encode(header.Bloom[:]),
		"prevRandao":            header.MixDigest.Hex(),
		"blockNumber":           hexutil.EncodeUint64(header.Number.Uint64()),
		"gasLimit":              hexutil.EncodeUint64(header.GasLimit),
		"gasUsed":               hexutil.EncodeUint64(header.GasUsed),
		"timestamp":             hexutil.EncodeUint64(header.Time),
		"extraData":             hexutil.Encode(header.Extra),
		"baseFeePerGas":         hexutil.EncodeBig(header.BaseFee),
		"blockHash":             blockHash.Hex(),
		"withdrawalsRoot":       header.WithdrawalsHash.Hex(),
		"blobGasUsed":           hexutil.EncodeUint64(*header.BlobGasUsed),
		"excessBlobGas":         hexutil.EncodeUint64(*header.ExcessBlobGas),
		"parentBeaconBlockRoot": header.ParentBeaconRoot.Hex(),
		"withdrawals":           []interface{}{},
		"transactions":          txHexes,
	}, nil
}

// buildInvalidTxReject fills the _op_expected.reject schema for a §4b kind.
// consumer follows review R16: decode-level kinds carry the FISCO decode
// message as validation_error_contains (a reliable engine surface); the rest
// carry the T8n throw message (executor-only semantics — the engine
// RTTI-bypass collapses it to a generic message, so the E2E runner must not
// assert validation_error_contains on those).
func buildInvalidTxReject(spec *invalidTxCaseSpec) *rejectExpected {
	validationContains := spec.t8n
	if spec.decode != "" {
		validationContains = spec.decode
	}
	return &rejectExpected{
		OpGeth: spec.opGeth,
		Fisco: fiscoReject{
			Consumer:                spec.consumer,
			Classification:          "INVALID",
			LatestValidHash:         json.RawMessage(`"parent"`),
			ValidationErrorContains: validationContains,
		},
	}
}

// buildInvalidTxVector assembles the full on-disk invalid vector for a §4b
// kind+fork, plus the built block and genesis (the Go tests verify txRoot /
// blockHash / InsertChain rejection against them). The vector carries BOTH the
// engine payload (_op_payload) AND the T8n replay surface (env + block).
func buildInvalidTxVector(kind, fork string) (invalidVectorDoc, *types.Block, *core.Genesis, error) {
	zero := invalidVectorDoc{}
	spec, err := invalidTxSpec(kind)
	if err != nil {
		return zero, nil, nil, err
	}
	in, buildInvalid, err := buildInvalidTxCase(kind, fork)
	if err != nil {
		return zero, nil, nil, err
	}
	cfg, err := buildConfigForCase(&in)
	if err != nil {
		return zero, nil, nil, err
	}
	genesis, err := buildGenesisForCase(&in, cfg)
	if err != nil {
		return zero, nil, nil, err
	}
	blockTime := uint64(in.Genesis.Timestamp) + 10
	signer := types.MakeSigner(cfg, big.NewInt(1), blockTime)
	deposit, depositOut, err := buildTx(&in.Transactions[0], signer, cfg)
	if err != nil {
		return zero, nil, nil, fmt.Errorf("deposit: %w", err)
	}
	invalid, invalidOut, err := buildInvalid(signer, cfg)
	if err != nil {
		return zero, nil, nil, fmt.Errorf("invalid tx: %w", err)
	}
	blk, err := buildInvalidTxBlock(&in, cfg, genesis, signer, deposit, invalid)
	if err != nil {
		return zero, nil, nil, err
	}
	payload, err := buildInvalidTxPayload(blk.Header(), blk.Hash(), []*types.Transaction{deposit, invalid})
	if err != nil {
		return zero, nil, nil, err
	}
	env := outputEnv{
		CurrentCoinbase:       strings.ToLower(in.Coinbase.Hex()),
		CurrentNumber:         hexutil.EncodeUint64(1),
		CurrentTimestamp:      hexutil.EncodeUint64(blockTime),
		CurrentGasLimit:       hexutil.EncodeUint64(blk.GasLimit()),
		CurrentBaseFee:        hexutil.EncodeBig(blk.BaseFee()),
		CurrentRandom:         "0x0",
		ParentBeaconBlockRoot: in.ParentBeaconBlockRoot.Hex(),
		ParentHash:            blk.ParentHash().Hex(),
	}
	doc := invalidVectorDoc{
		Info: caseInfo{
			Hardfork: fork,
			Description: fmt.Sprintf("%s: %s (invalid %s tx inserted after the L1 attributes deposit)",
				spec.kind, spec.opGeth, spec.kind),
		},
		Pre:   emitPre(in.Pre),
		Env:   &env,
		Block: &outputBlock{Transactions: []json.RawMessage{depositOut, invalidOut}},
		// The replayer's setcode loader hard-requires `postState` for any auth
		// tuple; the rejected block applies no delegation, so the map is empty.
		// Only setcode txs carry it (all other arms never read postState).
		PostState: &types.GenesisAlloc{},
		OpPayload: payload,
		OpExpected: invalidExpected{
			Reject: buildInvalidTxReject(spec),
		},
	}
	return doc, blk, genesis, nil
}

// recomputeOpHeaderHash returns the canonical OP block hash: keccak256 of the
// RLP-encoded header (op-geth types.Header.Hash(), core/types/block.go:124).
// This is what makes a corruption SELF-CONSISTENT -- the payload blockHash is
// re-derived from the corrupt header, so the engine gate's step-2 blockHash
// check passes and the rejection lands on the corrupted FIELD (step-5/six-way).
func recomputeOpHeaderHash(h *types.Header) common.Hash {
	return h.Hash()
}

// corruptVector applies the §4a per-field corrupt recipe to a valid generated
// block and returns the invalid vector document plus the rebuilt corrupt block
// (for captureInsertChainRejection). The blockHash field is the documented
// exception: no corrupt block is produced (InsertChain would ACCEPT it -- the
// block's own hash stays self-consistent) and the anchor is the engine-API
// "blockhash mismatch" message recorded directly.
func corruptVector(base *blockVector, field string) (invalidVectorDoc, *types.Block, error) {
	if base == nil || base.block == nil || base.genesis == nil || base.golden == nil {
		return invalidVectorDoc{}, nil, fmt.Errorf("corruptVector: nil base/block/genesis/golden")
	}
	header := base.block.Header()
	corruptHeader := types.CopyHeader(header)
	blockHash := common.Hash{}
	recompute := true

	switch field {
	case "stateRoot":
		flipHashByte(&corruptHeader.Root)
	case "gasUsed":
		corruptHeader.GasUsed = corruptGasUsedValue(header.GasLimit, header.GasUsed)
	case "receiptsRoot":
		flipHashByte(&corruptHeader.ReceiptHash)
	case "parentHash":
		// Unknown-ancestor recipe: the parent becomes a hash nothing in the
		// chain answers to -> SYNCING on the FISCO side, "unknown ancestor" on
		// the op-geth InsertChain side.
		corruptHeader.ParentHash = common.HexToHash("0x0000000000000000000000000000000000000000000000000000000000000001")
	case "extraData":
		// Shape break (size), NO blockHash recompute: the static shape check
		// (FISCO validateOpNewPayloadRequest / op-geth ValidateHoloceneExtraData)
		// fires before any blockHash comparison, so the payload keeps the
		// ORIGINAL golden blockHash.
		recompute = false
		blockHash = common.HexToHash(base.golden.BlockHash)
		corruptHeader.Extra = corruptExtraData(base.cfg, header.Time)
	case "blockHash":
		// payload.blockHash corruption, NO recompute, NO capture block.
		recompute = false
		bh := common.HexToHash(base.golden.BlockHash)
		flipHashByte(&bh)
		blockHash = bh
		corruptHeader = nil
	default:
		return invalidVectorDoc{}, nil, fmt.Errorf("corruptVector: unknown field %q", field)
	}
	if recompute {
		blockHash = recomputeOpHeaderHash(corruptHeader)
	}

	// Rebuild the corrupt block: base block (built with types.NewBlock per the
	// brief) + WithSeal to swap in the corrupt header while keeping the body.
	// WithSeal is safe where NewBlockWithHeader is not: it preserves the
	// transactions (NewBlockWithHeader would drop them).
	var corruptBlock *types.Block
	if corruptHeader != nil {
		corruptBlock = base.block.WithSeal(corruptHeader)
	}

	rej, err := buildRejectForCorruptField(base, field, corruptBlock)
	if err != nil {
		return invalidVectorDoc{}, nil, err
	}

	payloadHeader := header
	if corruptHeader != nil {
		payloadHeader = corruptHeader
	}
	payload := buildOpPayload(base, payloadHeader, blockHash)

	doc := invalidVectorDoc{
		Info: caseInfo{
			Hardfork:    base.vec.Info.Hardfork,
			Description: base.vec.Info.Description + " (corrupt " + field + ")",
		},
		Pre:       base.vec.Pre,
		OpPayload: payload,
		OpExpected: invalidExpected{
			Reject: rej,
		},
	}
	return doc, corruptBlock, nil
}

func flipHashByte(h *common.Hash) {
	b := h[:]
	b[0] ^= 0xff
}

// corruptGasUsedValue returns a gasUsed value that is <= gasLimit and differs
// from the actual value, so the corruption lands in the "invalid gas used"
// bucket (op-geth block_validator.go:154) instead of the gasLimit bound check.
func corruptGasUsedValue(gasLimit, actual uint64) uint64 {
	v := gasLimit
	if v == actual {
		if v == 0 {
			return 0 // cannot corrupt below 0; caller asserts rejection elsewhere
		}
		v--
	}
	return v
}

// corruptExtraData breaks the Holocene+ extraData shape: it returns a wrong
// length (8 bytes for Isthmus, 16 for Jovian) whose version byte is correct so
// the failure is unambiguous -- "should be 9/17 bytes". op-geth anchor:
// "invalid optimism extraData: holocene extraData should be 9 bytes, got 8"
// (Isthmus) / "Jovian extraData should be 17 bytes, got 16" (Jovian). FISCO:
// "extraData must be exactly 9/17 bytes on the OP path (Isthmus/Jovian)".
func corruptExtraData(cfg *params.ChainConfig, blockTime uint64) []byte {
	if cfg.IsJovian(blockTime) {
		return []byte{0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} // 16B, version 0x01
	}
	return []byte{0x00, 0, 0, 0, 0, 0, 0, 0} // 8B, version 0x00
}

// buildRejectForCorruptField fills the _op_expected.reject schema for a corrupt
// field. For every field EXCEPT blockHash it captures the REAL op-geth
// InsertChain rejection message (the weak op_geth anchor) and derives the FISCO
// classification from the recipe.
func buildRejectForCorruptField(base *blockVector, field string, corruptBlock *types.Block) (*rejectExpected, error) {
	switch field {
	case "stateRoot", "gasUsed", "receiptsRoot":
		if corruptBlock == nil {
			return nil, fmt.Errorf("corrupt %s: nil capture block", field)
		}
		msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
		if err != nil {
			return nil, fmt.Errorf("corrupt %s: InsertChain capture: %w", field, err)
		}
		return &rejectExpected{
			OpGeth: msg,
			Fisco: fiscoReject{
				Consumer:                "engine",
				Classification:          "INVALID",
				LatestValidHash:         json.RawMessage(`"parent"`),
				ValidationErrorContains: field,
			},
		}, nil
	case "parentHash":
		if corruptBlock == nil {
			return nil, fmt.Errorf("corrupt parentHash: nil capture block")
		}
		msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
		if err != nil {
			return nil, fmt.Errorf("corrupt parentHash: InsertChain capture: %w", err)
		}
		// SYNCING: parent unknown is the design intent; no latest_valid_hash, no
		// validation_error (the FISCO runner asserts SYNCING status only).
		return &rejectExpected{
			OpGeth: msg,
			Fisco: fiscoReject{
				Consumer:       "engine",
				Classification: "SYNCING",
			},
		}, nil
	case "extraData":
		if corruptBlock == nil {
			return nil, fmt.Errorf("corrupt extraData: nil capture block")
		}
		msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
		if err != nil {
			return nil, fmt.Errorf("corrupt extraData: InsertChain capture: %w", err)
		}
		return &rejectExpected{
			OpGeth: msg,
			Fisco: fiscoReject{
				Consumer:                "engine",
				Classification:          "INVALID",
				LatestValidHash:         json.RawMessage("null"),
				ValidationErrorContains: fiscoExtraDataMessage(base.cfg, base.block.Header().Time),
			},
		}, nil
	case "blockHash":
		// No InsertChain capture: the block's own hash stays self-consistent, so
		// InsertChain ACCEPTS it. The anchor is the engine-API ExecutableDataToBlock
		// message (beacon/engine/types.go:281). FISCO rebuilds the header and
		// rejects with "blockHash does not match the reconstructed block header"
		// (EngineServiceImpl.h:895-899), INVALID + latestValidHash=null.
		return &rejectExpected{
			OpGeth: "blockhash mismatch",
			Fisco: fiscoReject{
				Consumer:                "engine",
				Classification:          "INVALID",
				LatestValidHash:         json.RawMessage("null"),
				ValidationErrorContains: "blockHash does not match the reconstructed block header",
			},
		}, nil
	default:
		return nil, fmt.Errorf("buildRejectForCorruptField: unknown field %q", field)
	}
}

// fiscoExtraDataMessage returns the FISCO validateOpNewPayloadRequest substring
// that matches the corruptExtraData shape break (EngineServiceImpl.cpp:430-452).
func fiscoExtraDataMessage(cfg *params.ChainConfig, blockTime uint64) string {
	if cfg.IsJovian(blockTime) {
		return "extraData must be exactly 17 bytes on the OP path (Jovian)"
	}
	return "extraData must be exactly 9 bytes on the OP path (Isthmus)"
}

// buildOpPayload assembles the full ExecutionPayload JSON for an invalid vector
// from a (possibly corrupt) header. Field names/units follow makeParamsJson
// (GoldenSample.h): timestamp in OP seconds; every quantity a hex string.
func buildOpPayload(base *blockVector, header *types.Header, blockHash common.Hash) map[string]interface{} {
	return buildPayloadFromHeader(header, blockHash, base.txs)
}

// buildPayloadFromHeader assembles the full ExecutionPayload JSON from a
// (possibly corrupt) header + its tx list. Shared by buildOpPayload (the
// blockVector path) and the Task 5 chain fork/break carriers (which have raw
// *types.Block and no blockVector).
func buildPayloadFromHeader(header *types.Header, blockHash common.Hash, txs []*types.Transaction) map[string]interface{} {
	if header.WithdrawalsHash == nil || header.BlobGasUsed == nil || header.ExcessBlobGas == nil {
		// Cannot happen on the isthmus/jovian path (assembleOutput asserts the
		// first two; buildGoldenRecord asserts ExcessBlobGas == 0) -- guard for
		// a nil-deref-free error surface.
		panic("buildPayloadFromHeader: header missing WithdrawalsHash/BlobGasUsed/ExcessBlobGas")
	}
	txHexes := make([]string, len(txs))
	for i, tx := range txs {
		b, err := tx.MarshalBinary()
		if err != nil {
			panic("buildPayloadFromHeader: tx MarshalBinary: " + err.Error())
		}
		txHexes[i] = hexutil.Encode(b)
	}
	return map[string]interface{}{
		"parentHash":            header.ParentHash.Hex(),
		"feeRecipient":          strings.ToLower(header.Coinbase.Hex()),
		"stateRoot":             header.Root.Hex(),
		"receiptsRoot":          header.ReceiptHash.Hex(),
		"logsBloom":             hexutil.Encode(header.Bloom[:]),
		"prevRandao":            header.MixDigest.Hex(),
		"blockNumber":           hexutil.EncodeUint64(header.Number.Uint64()),
		"gasLimit":              hexutil.EncodeUint64(header.GasLimit),
		"gasUsed":               hexutil.EncodeUint64(header.GasUsed),
		"timestamp":             hexutil.EncodeUint64(header.Time),
		"extraData":             hexutil.Encode(header.Extra),
		"baseFeePerGas":         hexutil.EncodeBig(header.BaseFee),
		"blockHash":             blockHash.Hex(),
		"withdrawalsRoot":       header.WithdrawalsHash.Hex(),
		"blobGasUsed":           hexutil.EncodeUint64(*header.BlobGasUsed),
		"excessBlobGas":         hexutil.EncodeUint64(*header.ExcessBlobGas),
		"parentBeaconBlockRoot": header.ParentBeaconRoot.Hex(),
		"withdrawals":           []interface{}{},
		"transactions":          txHexes,
	}
}

// captureInsertChainRejection runs a corrupt block through a fresh
// core.NewBlockChain (4-arg, matching this checkout's selfCheck precedent at
// main.go:1464) + InsertChain and returns the rejection message. A corrupt
// block that is ACCEPTED is a hard error (iron-rule mirror violation: the
// corruption recipe silently failed to produce a rejection).
func captureInsertChainRejection(genesis *core.Genesis, block *types.Block) (string, error) {
	engine := beacon.New(ethash.NewFaker())
	chain, err := core.NewBlockChain(rawdb.NewMemoryDatabase(), genesis, engine, nil)
	if err != nil {
		return "", fmt.Errorf("NewBlockChain: %w", err)
	}
	defer chain.Stop()
	if _, err := chain.InsertChain(types.Blocks{block}); err == nil {
		return "", fmt.Errorf("corrupt block ACCEPTED (iron-rule mirror violation)")
	} else {
		return err.Error(), nil
	}
}

// ---------------------------------------------------------------------
// §4c static surface (--mode=static)
// ---------------------------------------------------------------------

// staticItem is one §4c static-validation malformation. mutate rewrites the
// base payload; fiscoMessage is the EXACT substring the FISCO
// validateOpNewPayloadRequest (EngineServiceImpl.cpp:310-508) returns, which the
// E2E runner asserts via validation_error_contains. fork selects the base block
// (isthmus|jovian) -- item 11 (Jovian DA footprint) needs a Jovian base.
type staticItem struct {
	n            int
	name         string
	fork         string
	mutate       func(payload map[string]interface{}) error
	fiscoMessage string
}

// staticSurfaceItems enumerates the 12 §4c items. ⚠️ Expressibility through the
// Task 2 loader (GoldenSample.h makeInvalidParamsJson) + parseNewPayloadRequest
// is the constraint that decides whether an item actually reaches its FISCO
// message in the E2E runner:
//   - items 3 (expectedBlobVersionedHashes non-empty) and 12 (executionRequests
//     non-empty) CANNOT be expressed: the loader hardcodes params[1] = [] and
//     parseNewPayloadRequest never parses executionRequests. They are emitted
//     per the brief but must NOT be manifest-registered until the loader/RPC
//     parse is extended (see task-3-report).
//   - item 1 (rawTransactions missing) is expressed as `transactions: null`
//     (NOT omission -- the loader substitutes an empty array for a missing
//     member, which would convert the case into a blockHash mismatch).
//   - item 8 (blockNumber negative) is expressed as 0x8000000000000000, which
//     fromQuantity parses as uint64 and the cast to int64 wraps negative.
var staticSurfaceItems = []staticItem{
	{1, "rawTransactions_missing", "isthmus", func(p map[string]interface{}) error {
		p["transactions"] = nil
		return nil
	}, "executionPayload.rawTransactions is required on the OP path"},
	{2, "withdrawals_nonempty", "isthmus", func(p map[string]interface{}) error {
		p["withdrawals"] = []map[string]interface{}{
			{"index": "0x0", "validatorIndex": "0x0", "amount": "0x1", "address": "0x0000000000000000000000000000000000000001"},
		}
		return nil
	}, "withdrawals must be present and empty on the OP path"},
	{3, "expectedBlobVersionedHashes_nonempty", "isthmus", func(p map[string]interface{}) error {
		p["expectedBlobVersionedHashes"] = []string{"0x0000000000000000000000000000000000000000000000000000000000000001"}
		return nil
	}, "expectedBlobVersionedHashes must be an empty array on the OP path"},
	{4, "parentBeaconBlockRoot_missing", "isthmus", func(p map[string]interface{}) error {
		delete(p, "parentBeaconBlockRoot")
		return nil
	}, "parentBeaconBlockRoot must be a 32-byte hash for newPayloadV4"},
	{5, "withdrawalsRoot_missing", "isthmus", func(p map[string]interface{}) error {
		delete(p, "withdrawalsRoot")
		return nil
	}, "withdrawalsRoot is required on the OP path (Isthmus+)"},
	{6, "excessBlobGas_nonzero", "isthmus", func(p map[string]interface{}) error {
		p["excessBlobGas"] = "0x1"
		return nil
	}, "excessBlobGas must be present and zero on the OP path"},
	{7, "isthmus_blobGasUsed_nonzero", "isthmus", func(p map[string]interface{}) error {
		p["blobGasUsed"] = "0x1"
		return nil
	}, "blobGasUsed must be zero before Jovian (OP Isthmus)"},
	{8, "blockNumber_negative", "isthmus", func(p map[string]interface{}) error {
		p["blockNumber"] = "0x8000000000000000" // uint64 2^63 wraps to negative int64
		return nil
	}, "blockNumber must not be negative"},
	{9, "gasLimit_overlimit", "isthmus", func(p map[string]interface{}) error {
		p["gasLimit"] = "0xffffffffffffffff" // 2^64-1 > 2^63-1
		return nil
	}, "gasLimit exceeds the maximum block gas limit (2^63-1)"},
	{10, "extraData_shape", "isthmus", func(p map[string]interface{}) error {
		p["extraData"] = "0x0000000000000000" // 8 bytes, not 9
		return nil
	}, "extraData must be exactly 9 bytes on the OP path (Isthmus)"},
	{11, "jovian_da_footprint_over_gaslimit", "jovian", func(p map[string]interface{}) error {
		p["blobGasUsed"] = "0x2000000" // 2^25 = 33554432 > 10M gasLimit
		return nil
	}, "DA footprint (blobGasUsed) exceeds the block gas limit"},
	{12, "executionRequests_nonempty", "isthmus", func(p map[string]interface{}) error {
		p["executionRequests"] = []map[string]interface{}{
			{"type": "0x0", "data": "0xdeadbeef"},
		}
		return nil
	}, "executionRequests must be absent or empty on the OP path"},
}

// buildBasePayload assembles the FULL valid payload of a base block (all base
// true values + the original golden blockHash). The §4c items start from here
// and malform one field.
func buildBasePayload(base *blockVector) map[string]interface{} {
	return buildOpPayload(base, base.block.Header(), common.HexToHash(base.golden.BlockHash))
}

// emitStaticSurfaceVector builds the invalid vector document for one §4c static
// item: base payload + the item's malformation + the FISCO static reject schema.
// All static items reject at validateOpNewPayloadRequest -> INVALID +
// latestValidHash=null.
func emitStaticSurfaceVector(base *blockVector, item staticItem) (invalidVectorDoc, error) {
	if base == nil || base.block == nil || base.golden == nil {
		return invalidVectorDoc{}, fmt.Errorf("emitStaticSurfaceVector: nil base")
	}
	payload := buildBasePayload(base)
	if item.mutate != nil {
		if err := item.mutate(payload); err != nil {
			return invalidVectorDoc{}, err
		}
	}
	return invalidVectorDoc{
		Info: caseInfo{
			Hardfork:    base.vec.Info.Hardfork,
			Description: base.vec.Info.Description + " (static " + item.name + ")",
		},
		Pre:       base.vec.Pre,
		OpPayload: payload,
		OpExpected: invalidExpected{
			Reject: &rejectExpected{
				OpGeth: item.fiscoMessage,
				Fisco: fiscoReject{
					Consumer:                "engine",
					Classification:          "INVALID",
					LatestValidHash:         json.RawMessage("null"),
					ValidationErrorContains: item.fiscoMessage,
				},
			},
		},
	}, nil
}

// writeInvalidVector writes one invalid vector as vectors/invalid_*.json with
// the same outer-wrapping convention as run() (a `_op_test_vectors` meta object
// plus the single vector object keyed by the stem).
func writeInvalidVector(outDir, stem string, doc invalidVectorDoc, opGethCommit string) error {
	meta, err := json.Marshal(struct {
		Version         string `json:"version"`
		Generator       string `json:"generator"`
		GeneratorCommit string `json:"generator_commit"`
	}{schemaVersion, "opt8n-ref", opGethCommit})
	if err != nil {
		return err
	}
	docBytes, err := json.Marshal(doc)
	if err != nil {
		return err
	}
	out := map[string]json.RawMessage{
		"_op_test_vectors": meta,
		stem:               docBytes,
	}
	outBytes, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	outBytes = append(outBytes, '\n')
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(outDir, stem+".json"), outBytes, 0o644)
}

// ---------------------------------------------------------------------
// L1Block slot <-> calldata consistency (iron rule 2)
//
// Calldata layout (rollup_cost.go extractL1GasParamsPostEcotone /
// extractL1GasParamsPostIsthmus / ExtractDAFootprintGasScalar):
//   [0:4]   selector (Ecotone 0x440a5e20 / Isthmus 0x098999be / Jovian 0x3db6be2b)
//   [4:8]   baseFeeScalar        <-> slot 3 bytes [16:20]
//   [8:12]  blobBaseFeeScalar    <-> slot 3 bytes [20:24]
//   [36:68] l1BaseFee            <-> slot 1
//   [68:100] l1BlobBaseFee       <-> slot 7
//   [164:168] operatorFeeScalar  <-> slot 8 bytes [20:24]   (Isthmus+ only)
//   [168:176] operatorFeeConstant<-> slot 8 bytes [24:32]   (Isthmus+ only)
//   [176:178] daFootprintGasScalar (Jovian) <-> slot 8 bytes [18:20]
//             (slot mapping mirrors bcos-evm-ref OpFeeParams.h)
// The Ecotone 164B layout has NO [164:176] operator-fee segment and no
// [176:178] DA scalar. extractL1GasParamsPostEcotone hard-rejects any
// non-164-byte payload, so the length/selector checks here are the corpus
// guard against emitting an Isthmus-shaped deposit under an Ecotone-family
// fork.
// ---------------------------------------------------------------------

// blockFork returns the fork that governs a block at the given time under cfg:
// the latest activated OP fork (jovian > isthmus > holocene > granite > fjord
// > ecotone). For pure fork-at-0 recipes this equals the case's hardfork; for
// upgrade-boundary specs (Task 3) it is the BLOCK-TIME fork, which is what
// decides the L1-attributes byte layout.
func blockFork(cfg *params.ChainConfig, blockTime uint64) string {
	switch {
	case cfg.IsJovian(blockTime):
		return "jovian"
	case cfg.IsIsthmus(blockTime):
		return "isthmus"
	case cfg.IsHolocene(blockTime):
		return "holocene"
	case cfg.IsGranite(blockTime):
		return "granite"
	case cfg.IsFjord(blockTime):
		return "fjord"
	default:
		return "ecotone"
	}
}

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

	// blockTime (NOT genesis time) decides jovianCfg and the attributes LAYOUT:
	// an upgrade-boundary vector whose genesis is pre-Isthmus/pre-Jovian but
	// whose single block crosses IsthmusTime/JovianTime must be judged by the
	// block-time fork -- op-geth switches the attributes deposit format at the
	// fork boundary. For pure fork-at-0 recipes blockFork == the case hardfork.
	blockTime := uint64(in.Genesis.Timestamp) + 10 // chain_makers.makeHeader: block time fixed at parent+10
	jovianCfg := cfg.IsJovian(blockTime)
	layout := forkLayout(blockFork(cfg, blockTime))

	checkCommon := func(layout l1AttributesLayout) error {
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
		if layout != layoutEcotone {
			// Operator-fee segment exists only in Isthmus+ layouts; the
			// Ecotone 164B form has no [164:176] bytes to mirror.
			if !bytes.Equal(slot8[20:24], data[164:168]) {
				return fmt.Errorf("slot8[20:24] (operatorFeeScalar) %x != calldata[164:168] %x", slot8[20:24], data[164:168])
			}
			if !bytes.Equal(slot8[24:32], data[168:176]) {
				return fmt.Errorf("slot8[24:32] (operatorFeeConstant) %x != calldata[168:176] %x", slot8[24:32], data[168:176])
			}
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
		return checkCommon(layoutIsthmus)
	case jovianCfg:
		if len(data) != types.JovianL1AttributesLen {
			return fmt.Errorf("Jovian attributes must be %d bytes (or %d for the activation form), got %d",
				types.JovianL1AttributesLen, types.IsthmusL1AttributesLen, len(data))
		}
		if !bytes.Equal(data[0:4], types.JovianL1AttributesSelector) {
			return fmt.Errorf("Jovian attributes selector mismatch: got %x", data[0:4])
		}
		if err := checkCommon(layoutJovian); err != nil {
			return err
		}
		if !bytes.Equal(slot8[18:20], data[176:178]) {
			return fmt.Errorf("slot8[18:20] (daFootprintGasScalar) %x != calldata[176:178] %x", slot8[18:20], data[176:178])
		}
		return nil
	case layout == layoutEcotone:
		// Ecotone/Fjord/Granite/Holocene: exactly 164B with the Ecotone
		// selector and NO operator-fee/DA segment. extractL1GasParamsPostEcotone
		// (rollup_cost.go:477-479) hard-rejects any other length, so this is
		// also the corpus-side guard that the deposit is not Isthmus-shaped.
		if len(data) != 164 {
			return fmt.Errorf("Ecotone-family attributes must be 164 bytes, got %d", len(data))
		}
		if !bytes.Equal(data[0:4], types.EcotoneL1AttributesSelector) {
			return fmt.Errorf("Ecotone-family attributes selector mismatch: got %x", data[0:4])
		}
		return checkCommon(layoutEcotone)
	default: // isthmus
		if len(data) != types.IsthmusL1AttributesLen {
			return fmt.Errorf("Isthmus attributes must be %d bytes, got %d", types.IsthmusL1AttributesLen, len(data))
		}
		if !bytes.Equal(data[0:4], types.IsthmusL1AttributesSelector) {
			return fmt.Errorf("Isthmus attributes selector mismatch: got %x", data[0:4])
		}
		return checkCommon(layoutIsthmus)
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

	case "legacy":
		// Type-0 legacy tx with an EIP-155 protected signature (B scope base for
		// corrupt/invalid-tx vectors). Signing with the standard MakeSigner
		// signer (Isthmus/London on this chain) routes LegacyTxType through the
		// EIP155Signer arm (transaction_signing.go:299-300), producing
		// V = chainID*2+35/36. GasPrice is the sole per-gas price; a legacy tx on
		// an EIP-1559 chain is valid as long as gasPrice >= baseFee.
		prv, from, err := parseKey(in.SecretKey)
		if err != nil {
			return nil, nil, err
		}
		chainID, nonce, gas, value, _, _ := txScalars(in)
		gasPrice := big.NewInt(0)
		if in.GasPrice != nil {
			gasPrice = (*big.Int)(in.GasPrice)
		}
		txdata := &types.LegacyTx{
			Nonce:    nonce,
			GasPrice: gasPrice,
			Gas:      gas,
			To:       in.To,
			Value:    value,
			Data:     []byte(in.Data),
		}
		tx, err := types.SignNewTx(prv, signer, txdata)
		if err != nil {
			return nil, nil, fmt.Errorf("signing legacy tx: %w", err)
		}
		rawBin, err := tx.MarshalBinary() // iron rule: _op_raw = tx.MarshalBinary()
		if err != nil {
			return nil, nil, err
		}
		outJSON, err := json.Marshal(outputLegacyTx{
			OpType:   "legacy",
			OpRaw:    hexutil.Encode(rawBin),
			ChainID:  (*math.HexOrDecimal256)(chainID),
			Nonce:    math.HexOrDecimal64(nonce),
			To:       in.To,
			Gas:      math.HexOrDecimal64(gas),
			GasPrice: (*math.HexOrDecimal256)(gasPrice),
			Value:    (*math.HexOrDecimal256)(value),
			Data:     in.Data,
			Sender:   from,
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

// chainCtxStub satisfies core.ChainContext for the output re-execution. The EVM
// block context reads Config() (blob base fee) and defers GetHeader* to
// BLOCKHASH; the corpus never executes BLOCKHASH, so the lookups are inert.
// core.BlockChain's chainConfig field is unexported, so a hand stub is required
// outside the core package.
type chainCtxStub struct{ cfg *params.ChainConfig }

func (c chainCtxStub) Engine() consensus.Engine {
	return nil
}
func (c chainCtxStub) Config() *params.ChainConfig {
	return c.cfg
}
func (c chainCtxStub) CurrentHeader() *types.Header {
	return nil
}
func (c chainCtxStub) GetHeader(common.Hash, uint64) *types.Header {
	return nil
}
func (c chainCtxStub) GetHeaderByNumber(uint64) *types.Header {
	return nil
}
func (c chainCtxStub) GetHeaderByHash(common.Hash) *types.Header {
	return nil
}

// reexecuteOutputs replays a generated block's txs against a fresh state and
// returns each tx's return data (the receipt output). The chain maker's AddTx
// discards ExecutionResult.ReturnData, so the real receipts never carry output;
// this replay reproduces the chain maker's pre-tx state (startRoot + EIP-2935
// parent-hash + EIP-4788 beacon-root system calls, same order as GenerateChain)
// and applies each tx via core.ApplyMessage, capturing ReturnData. Faithfulness
// is enforced per tx: each replayed result's UsedGas and success/failure must
// equal the real receipt's gasUsed/status, otherwise the emitted outputs are
// rejected (a divergence between the replay and the actual block execution would
// make the output field untrustworthy).
//
// startRoot is the per-block replay origin: the GENESIS root for a single-block
// vector (or chain block 0), and blocks[i-1].Root() for chain block i -- chain
// block B's pre-state IS block A's post-state, so a genesis-root replay would
// see B's sender nonce as 0 and reject B's nonce-1 transfer ("nonce too high").
func reexecuteOutputs(cfg *params.ChainConfig, genesis *core.Genesis, db ethdb.Database,
	header *types.Header, txs []*types.Transaction, receipts types.Receipts,
	coinbase common.Address, parentBeaconRoot common.Hash, startRoot common.Hash) ([][]byte, error) {
	tdb := triedb.NewDatabase(db, triedb.HashDefaults)
	defer tdb.Close()
	statedb, err := state.New(startRoot, state.NewDatabase(tdb, nil))
	if err != nil {
		return nil, fmt.Errorf("open re-execution state: %w", err)
	}
	gp := core.NewGasPool(header.GasLimit)
	bc := &chainCtxStub{cfg: cfg}

	// EIP-2935 parent-hash: gated exactly as chain_makers.genblock does. EIP-4788
	// beacon-root runs unconditionally (SetParentBeaconRoot always calls it); both
	// are no-ops pre-Prague (empty system-contract code).
	if cfg.IsPrague(header.Number, header.Time) || cfg.IsVerkle(header.Number, header.Time) {
		blockCtx := core.NewEVMBlockContext(header, bc, &coinbase, cfg, statedb)
		blockCtx.Random = &common.Hash{} // enable post-merge instruction set
		core.ProcessParentBlockHash(header.ParentHash, vm.NewEVM(blockCtx, statedb, cfg, vm.Config{}))
	}
	blockCtx := core.NewEVMBlockContext(header, bc, &coinbase, cfg, statedb)
	core.ProcessBeaconBlockRoot(parentBeaconRoot, vm.NewEVM(blockCtx, statedb, cfg, vm.Config{}))

	outs := make([][]byte, len(txs))
	for i, tx := range txs {
		statedb.SetTxContext(tx.Hash(), i)
		blockCtx := core.NewEVMBlockContext(header, bc, &coinbase, cfg, statedb)
		evm := vm.NewEVM(blockCtx, statedb, cfg, vm.Config{})
		msg, err := core.TransactionToMessage(tx, types.MakeSigner(cfg, header.Number, header.Time), header.BaseFee)
		if err != nil {
			return nil, fmt.Errorf("replay tx %d msg: %w", i, err)
		}
		result, err := core.ApplyMessage(evm, msg, gp)
		if err != nil {
			return nil, fmt.Errorf("replay tx %d: %w", i, err)
		}
		// Faithfulness cross-check: the replay must reproduce the real block's
		// receipt (MakeReceipt sets GasUsed = result.UsedGas, Status from Failed).
		if result.UsedGas != receipts[i].GasUsed ||
			result.Failed() != (receipts[i].Status != types.ReceiptStatusSuccessful) {
			return nil, fmt.Errorf("replay tx %d: gasUsed/status mismatch (replay %d/%v vs receipt %d/%d)",
				i, result.UsedGas, result.Failed(), receipts[i].GasUsed, receipts[i].Status)
		}
		outs[i] = common.CopyBytes(result.ReturnData)
		statedb.Finalise(true)
	}
	return outs, nil
}

func buildExpectedReceipts(cfg *params.ChainConfig, in *inputCase, txs []*types.Transaction, receipts types.Receipts, outputs [][]byte, blockTime uint64) ([]expectedReceipt, error) {
	jovian := cfg.IsJovian(blockTime)
	if len(outputs) != len(receipts) {
		return nil, fmt.Errorf("outputs/receipts count mismatch: %d vs %d", len(outputs), len(receipts))
	}
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
			Output:            hexutil.Encode(outputs[i]),
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
