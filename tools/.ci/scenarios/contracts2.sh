#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenarios 10 complex-abi (ComplexSol: int/string constructor, dynamic address
#             array, bytes, static bytes4, bytes[] mapping, multiple returns, event topics),
#             11 payable (deploy --value, incWithPayable --value, a non-payable method refuses
#             value) and 16 revert (require with and without a reason). Replaces java-sdk
#             AssembleTransactionProcessorTest ComplexSol cases, the on-chain parts of
#             CodecTest/CodecComplexTest, and TransactionManagerPayableTest; 16 is new.
# @ file    : contracts2.sh

# keccak256 of the event signatures (topics[0]); the SM chain hashes topics with sm3, where
# only the shape is asserted (no sm3 tool on the runner)
readonly c_topic_log_init="0x96cbce3c2f7574181c59fe1f42ee3733c5ebb6ff910ad138ec2877995da00b88"            # LogInit(address,string)
readonly c_topic_log_increment="0xaca9a02cfe513f3f88c54a860469369849c8fa0a2119a8d1f3f75c67ac0c9547"       # LogIncrement(address,uint256)
readonly c_topic_log_set_values="0x487b33fb5d5ff951b71eea40323f9d8dac61f7aefb4bc9c23399aa9cd900ba26"      # LogSetValues(int256,address[],string)
readonly c_topic_log_set_bytes="0xa0f5dc8c800a837e0a7c88af15df4a194b8cf2b9a1e0d4e1a28013471713ae13"       # LogSetBytes(bytes,bytes)
readonly c_topic_inc_event="0x7ed53cbadbbc7d8800605823ac88c67b7d9c9fa9d54d45a68d221760e0a86552"           # incEvent(string)

# ABI encoding of ComplexSol's constructor arguments (int256 7, string "init s"), appended
# to the bytecode by hand: cpp-sdk's ContractABICodec::encodeConstructor runs the constructor
# through encodeMethod, which prefixes a 4-byte selector that no constructor has
# (bcos-sdk/bcos-cpp-sdk/utilities/abi/ContractABICodec.cpp:69-73,111-119); the node then
# ABI-decodes garbage and reverts (status 16). With a constructor-less ABI copy `tx deploy`
# sends the bytes verbatim (Abi::encodeConstructor), so the arguments go in here. Drop this
# once the codec skips the selector for constructors.
readonly c_complex_ctor_args="000000000000000000000000000000000000000000000000000000000000000700000000000000000000000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000006696e697420730000000000000000000000000000000000000000000000000000"

# assert_topic <json> <jq path to topics[0]> <keccak topic> <chain> <what>
assert_topic()
{
    if [[ "${4}" == "sm" ]]; then
        assert_jq "${1}" "${2}"' | test("^0x[0-9a-f]{64}$")' "${5}: sm3 topic has the right shape" || return 1
    else
        assert_jq "${1}" "${2}"' == "'"${3}"'"' "${5}: topic[0] is keccak256 of the event signature" || return 1
    fi
}

# contract_abi_without_constructor <Name>: an ABI copy with no constructor entry, so
# `tx deploy` sends the given bytes verbatim; listed in ${ci_abi_list} like contract_abi's
contract_abi_without_constructor()
{
    local abi="$(contracts_dir)/${1}/${1}.abi"
    local stripped
    stripped=$(mktemp "${TMPDIR:-/tmp}/ci-abi-noctor-${1}.XXXXXX") || return 1
    jq 'map(select(.type != "constructor"))' "${abi}" > "${stripped}" || { rm -f "${stripped}"; return 1; }
    echo "${stripped}" >> "${ci_abi_list:-${TMPDIR:-/tmp}/ci-abi-files-$$}"
    echo "${stripped}"
}

# error_selector <chain>: the 4-byte selector of Error(string) — keccak256 on the keccak
# chains, sm3 on the SM chain (solc's sm3 build hashes selectors with sm3 as well)
error_selector()
{
    if [[ "${1}" == "sm" ]]; then echo "c703cb12"; else echo "08c379a0"; fi
}

# revert_reason <receipt output hex> <chain>: the string of an Error(string) revert, else empty
revert_reason()
{
    local out="${1#0x}"
    [[ "${out:0:8}" == "$(error_selector "${2}")" && ${#out} -ge 136 ]] || { echo ""; return 0; }
    local len=$((16#${out:72:64}))
    python3 -c 'import sys; print(bytes.fromhex(sys.argv[1]).decode("utf-8", "replace"))' "${out:136:$((len * 2))}"
}

scenario_10_complex_abi()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 10 complex-abi "java-sdk AssembleTransactionProcessorTest ComplexSol cases, CodecTest/CodecComplexTest on-chain parts"
    local abi deploy address
    abi=$(contract_abi ComplexSol) || return 1
    local ctor_abi
    ctor_abi=$(contract_abi_without_constructor ComplexSol) || return 1
    deploy=$(ops "${node_dir}" tx deploy "$(cat "$(contract_bin ComplexSol "${chain}")")${c_complex_ctor_args}" --abi "${ctor_abi}" --timeout 60000) || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0 and (.contractAddress | test("^0x[0-9a-fA-F]{40}$"))' "deploy ComplexSol(7, \"init s\")" || return 1
    assert_jq "${deploy}" '.logEntries | length == 1' "constructor emitted LogInit" || return 1
    assert_topic "${deploy}" '.logEntries[0].topics[0]' "${c_topic_log_init}" "${chain}" "LogInit" || return 1
    address=$(jq_get "${deploy}" '.contractAddress')
    local got
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" _intV) || return 1
    assert_jq "${got}" '.decoded == ["7"]' "constructor stored the int" || return 1
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" _s) || return 1
    assert_jq "${got}" '.decoded == ["init s"]' "constructor stored the string" || return 1
    # uint256 in, uint256 out, one event; the receipt is the same through tx get
    local sent
    sent=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" incrementUint256 41) || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and (.output | test("2a$"))' "incrementUint256(41) returns 42" || return 1
    assert_topic "${sent}" '.logEntries[0].topics[0]' "${c_topic_log_increment}" "${chain}" "LogIncrement" || return 1
    local fetched
    fetched=$(ops "${node_dir}" tx get "$(jq_get "${sent}" '.transactionHash')") || return 1
    assert_jq "${fetched}" '.receipt.status == 0 and (.receipt.logEntries | length == 1) and (.receipt.logEntries[0].data | length > 2)' "tx get carries the LogIncrement entry with data" || return 1
    assert_topic "${fetched}" '.receipt.logEntries[0].topics[0]' "${c_topic_log_increment}" "${chain}" "LogIncrement via tx get" || return 1
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" getUint256) || return 1
    assert_jq "${got}" '.decoded == ["42"]' "getUint256 reads 42" || return 1
    # negative int, dynamic address array, string: three values back from one call
    local -r a1="0x1111111111111111111111111111111111111111" a2="0x2222222222222222222222222222222222222222"
    sent=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" setValues -3 '["'"${a1}"'","'"${a2}"'"]' "vals") || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0' "setValues(-3, [a1, a2], \"vals\")" || return 1
    assert_topic "${sent}" '.logEntries[0].topics[0]' "${c_topic_log_set_values}" "${chain}" "LogSetValues" || return 1
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" getValues) || return 1
    assert_jq "${got}" '.decoded == ["-3", ["'"${a1}"'", "'"${a2}"'"], "vals"]' "getValues returns the three values" || return 1
    # dynamic bytes in and out (hex:// is the codec's bytes literal)
    sent=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" setBytes hex://abcdef) || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and (.output | test("0000000000000000000000000000000000000000000000000000000000000003abcdef0*$"))' "setBytes returns the 3 bytes" || return 1
    assert_topic "${sent}" '.logEntries[0].topics[0]' "${c_topic_log_set_bytes}" "${chain}" "LogSetBytes" || return 1
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" _bytesV) || return 1
    assert_jq "${got}" '.decoded == ["hex://abcdef"]' "_bytesV reads the bytes back" || return 1
    # static bytes4
    sent=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" setStaticByte4 hex://deadbeef) || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and (.output | test("1$"))' "setStaticByte4 returns true" || return 1
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" _bytes4V) || return 1
    assert_jq "${got}" '.decoded == ["hex://deadbeef"]' "_bytes4V reads the 4 bytes back" || return 1
    # bytes[] in, bytes[] out through a mapping
    sent=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" setBytesMapping '["hex://0102","hex://0304"]') || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and (.output | test("1$"))' "setBytesMapping([0102, 0304])" || return 1
    got=$(ops "${node_dir}" tx call "${address}" --abi "${abi}" getByBytes hex://0102) || return 1
    assert_jq "${got}" '.decoded == [["hex://0102", "hex://0304"]]' "getByBytes returns the stored bytes array" || return 1
    # static array return
    sent=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" getSArray) || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and .output == "0x00000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000002"' "getSArray returns [1, 2]" || return 1
    echo "scenario 10 ok: ComplexSol at ${address} on ${chain}"
}

scenario_11_payable()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 11 payable "java-sdk TransactionManagerPayableTest"
    local governor
    governor=$(chain_governor_pem "${node_dir}")
    [[ -f "${governor}" ]] || { scenario_fail "no governor pem for ${node_dir}"; return 1; }
    # the deployer needs a balance to attach value (scenario 8 turned the feature on)
    local acct_dir="$(dirname "${node_dir}")/../scenario11-acct"
    local payer payer_pem sm_flag=""
    [[ "${chain}" == "sm" ]] && sm_flag="--sm"
    payer=$("${fisco_bcos_path}" account new ${sm_flag} --out "${acct_dir}" --json | jq -r .address) || return 1
    payer_pem="${acct_dir}/${payer}.pem"
    local fund
    fund=$(ops "${node_dir}" balance add "${payer}" 1000 --yes --account "${governor}") || { echo "${fund}"; return 1; }
    assert_jq "${fund}" '.ok == true' "fund the payer with 1000" || return 1
    local abi deploy contract balance
    abi=$(contract_abi Payable) || return 1
    deploy=$(ops "${node_dir}" tx deploy "$(contract_bin Payable "${chain}")" --abi "${abi}" --value 10 --account "${payer_pem}") || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0 and (.contractAddress | test("^0x[0-9a-fA-F]{40}$"))' "deploy Payable with value 10" || return 1
    contract=$(jq_get "${deploy}" '.contractAddress')
    balance=$(ops "${node_dir}" balance get "${contract}") || return 1
    assert_jq "${balance}" '.value == "10"' "the contract holds the deploy value" || return 1
    balance=$(ops "${node_dir}" balance get "${payer}") || return 1
    assert_jq "${balance}" '.value == "990"' "the payer paid 10" || return 1
    local sent
    sent=$(ops "${node_dir}" tx send "${contract}" --abi "${abi}" incWithPayable "paid" --value 5 --account "${payer_pem}") || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and (.output | test("1$"))' "incWithPayable with value 5 returns 1" || return 1
    assert_topic "${sent}" '.logEntries[0].topics[0]' "${c_topic_inc_event}" "${chain}" "incEvent" || return 1
    balance=$(ops "${node_dir}" balance get "${contract}") || return 1
    assert_jq "${balance}" '.value == "15"' "the contract holds 15 after the payable call" || return 1
    balance=$(ops "${node_dir}" balance get "${payer}") || return 1
    assert_jq "${balance}" '.value == "985"' "the payer paid 5 more" || return 1
    local got
    got=$(ops "${node_dir}" tx call "${contract}" --abi "${abi}" value) || return 1
    assert_jq "${got}" '.decoded == ["1"]' "value() counted one call" || return 1
    # value on a non-payable method reverts and moves nothing
    local refused
    refused=$(ops_expect_exit 2 "${node_dir}" tx send "${contract}" --abi "${abi}" inc "unpaid" --value 5 --account "${payer_pem}") || return 1
    assert_jq "${refused}" '.status != 0' "inc with value reverts" || return 1
    balance=$(ops "${node_dir}" balance get "${contract}") || return 1
    assert_jq "${balance}" '.value == "15"' "the refused value stayed with the payer" || return 1
    echo "scenario 11 ok: Payable at ${contract} holds 15 on ${chain}"
}

scenario_16_revert()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 16 revert "new scenario: require(false, reason) and a bare revert()"
    local abi deploy contract
    abi=$(contract_abi Reverter) || return 1
    deploy=$(ops "${node_dir}" tx deploy "$(contract_bin Reverter "${chain}")" --abi "${abi}") || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0' "deploy Reverter" || return 1
    contract=$(jq_get "${deploy}" '.contractAddress')
    local sent
    sent=$(ops "${node_dir}" tx send "${contract}" --abi "${abi}" ok) || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0 and (.output | test("1$"))' "ok() counts to 1" || return 1
    # require(false, "reverted on purpose"): status 16 RevertInstruction, Error(string) output
    local failed reason
    local selector
    selector=$(error_selector "${chain}")
    failed=$(ops_expect_exit 2 "${node_dir}" tx send "${contract}" --abi "${abi}" fail) || return 1
    assert_jq "${failed}" '.status == 16 and (.output | startswith("0x'"${selector}"'"))' "fail() reverts with Error(string) (selector ${selector})" || return 1
    reason=$(revert_reason "$(jq_get "${failed}" '.output')" "${chain}")
    [[ "${reason}" == "reverted on purpose" ]] || { scenario_fail "revert reason '${reason}' != 'reverted on purpose'"; return 1; }
    local fetched
    fetched=$(ops "${node_dir}" tx get "$(jq_get "${failed}" '.transactionHash')") || return 1
    assert_jq "${fetched}" '.receipt.status == 16 and (.receipt.output | startswith("0x'"${selector}"'"))' "tx get shows the reverted receipt" || return 1
    # bare revert(): no reason
    failed=$(ops_expect_exit 2 "${node_dir}" tx send "${contract}" --abi "${abi}" failNoReason) || return 1
    assert_jq "${failed}" '.status == 16 and .output == "0x"' "failNoReason() reverts with empty output" || return 1
    # the reverted calls left no state behind
    local got
    got=$(ops "${node_dir}" tx call "${contract}" --abi "${abi}" counter) || return 1
    assert_jq "${got}" '.decoded == ["1"]' "counter untouched by the reverted calls" || return 1
    echo "scenario 16 ok: Reverter at ${contract} on ${chain}: '${reason}'"
}
