#include <bcos-evm/opstack/OpPrecompiles.h>

namespace bcos::evm::opstack
{
namespace
{
// All values below come from op-geth v1.101702.2; the citations are here so a future OP Stack
// change can be diffed against a specific line rather than re-derived.
//
// Input-size limits — params/protocol_params.go:
//   bn256Pairing  112687 (:172, Bn256PairingMaxInputSizeGranite)
//   BLS G1 MSM    513760 (:186)   G2 MSM  488448 (:187)   pairing  235008 (:188)  [Isthmus]
//   bn256Pairing   81984 (:194)   G1 MSM  288960 (:195)
//   G2 MSM        278784 (:196)   pairing 156672 (:197)                            [Jovian]
//
// Fork membership — core/vm/contracts.go:182-251. Isthmus reuses bn256PairingGranite, which is
// why its bn256 limit stays at the Granite value rather than getting one of its own; Jovian
// re-tightens all four.
//
// P256Verify gas 3450 = P256VerifyGasFjord (protocol_params.go:183), NOT the default
// P256VerifyGas 6900 (:184) — op-geth binds 0x100 to p256VerifyFjord from Fjord onward
// (contracts.go:193).
//
// Addresses 0x0c / 0x0e / 0x0f are EIP-2537 G1 MSM / G2 MSM / pairing. op-geth caps only the
// MSM and pairing precompiles; G1Add (0x0b), G2Add (0x0d) and the Map ops (0x10, 0x11) carry no
// limit, hence their absence from these tables.
constexpr PrecompileOverrides::Entry kIsthmusEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 513760},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 488448},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 235008},
};

constexpr PrecompileOverrides::Entry kJovianEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 81984},
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 288960},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 278784},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 156672},
};

// Karst re-tightens only bn256Pairing, and what it tightens is the INPUT SIZE, not the
// gas cost: 81984 -> 57600 BYTES of calldata. bn256 pairing consumes 192 bytes per pair,
// so those are Jovian's 427 pairs and Karst's 300 (81984/192 = 427, 57600/192 = 300) —
// matching Optimism Upgrade 19's "from 427 pairs (81,984 bytes) to 300 pairs (57,600
// bytes)". Pricing is untouched (gas_cost_override = -1 => no override); the per-pair gas
// stays whatever the vendored precompile charges. The BLS MSM/pairing limits carry over
// from Jovian unchanged. P256Verify deliberately has NO Karst entry: Karst adopts
// EIP-7951 (P256VERIFY at gas 6900), and karstConfig().rev is EVMC_OSAKA, so with no
// override OpHost::call falls through to the vendored Osaka-gated p256verify with exactly
// the EIP-7951 pricing — the pre-Karst 3450 (RIP-7212 P256VerifyGasFjord) entries stop at
// Jovian.
constexpr PrecompileOverrides::Entry kKarstEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 57600},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 288960},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 278784},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 156672},
};

// Fjord: only P256Verify. No bn256 limit yet (that arrives with Granite) and no BLS at all
// (CANCUN). Citations as above.
constexpr PrecompileOverrides::Entry kFjordEntries[] = {
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
};

constexpr PrecompileOverrides::Entry kGraniteEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
};
}  // namespace

const PrecompileOverrides& isthmusPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kIsthmusEntries};
    return overrides;
}

const PrecompileOverrides& jovianPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kJovianEntries};
    return overrides;
}

const PrecompileOverrides& karstPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kKarstEntries};
    return overrides;
}

const PrecompileOverrides& fjordPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kFjordEntries};
    return overrides;
}

const PrecompileOverrides& granitePrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kGraniteEntries};
    return overrides;
}
}  // namespace bcos::evm::opstack
