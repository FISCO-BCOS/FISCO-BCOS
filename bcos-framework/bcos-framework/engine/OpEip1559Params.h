#pragma once

#include <cstdint>
#include <optional>

namespace bcos::engine
{

/// A chain's EIP-1559 parameters, exactly as op-deployer writes them into the L2 genesis chain
/// config (`config.optimism`, the same values rollup.json calls chain_op_config). op-geth reads
/// them from the chain config (params/config.go:1349-1368 BaseFeeChangeDenominator /
/// ElasticityMultiplier) and they decide the base-fee step of EVERY descendant block
/// (consensus/misc/eip1559/eip1559.go:97, parentGasTarget = parent.GasLimit / elasticity).
///
/// `elasticity` has no per-fork split — op-geth's OptimismConfig carries exactly one and uses it
/// for every fork. The two denominators are selected by the NEW block's fork inside CalcBaseFee:
/// `IsCanyon(time) ? denominatorCanyon : denominator`.
struct OpEip1559Params
{
    std::uint64_t elasticity = 0;
    std::uint64_t denominator = 0;
    std::uint64_t denominatorCanyon = 0;
};

/// The OP mainnet preset op-deployer emits (op-deployer/pkg/deployer/standard/standard.go:32-34),
/// and the value this node hardcoded before the chain's parameters became configurable. A chain
/// that declares nothing keeps this triple, so its genesis pin and its pricing stay
/// byte-identical to the pre-change behaviour and existing chains keep starting.
///
/// It is also the pair the engine substitutes into a block's extraData when op-node reports zero
/// params and the node declares nothing (EngineServiceCommon.cpp encodeOptimismExtraData). The
/// reference substitutes from its chain config, i.e. `config.optimism` as op-deployer writes it —
/// for a standard deployment exactly this triple. op-geth has no else-branch for a chain config
/// carrying no Optimism section at all (it drops to the protocol constants 8/2/8,
/// params/config.go:1349-1368), but a real OP chain always has that section, so the preset is the
/// realistic stand-in. Keeping the value is deliberate: `effectiveOpEip1559` is shared by the
/// genesis pin (bcos-tool) and both pricing paths, so changing it would re-price every undeclared
/// chain and split it away from the pin. An undeclared OP-lane node logs a WARNING at boot and
/// again when the zero-param substitution fires, so the assumption is never silent.
inline constexpr OpEip1559Params kLegacyOpEip1559Params{.elasticity = 6,
    .denominator = 50,
    .denominatorCanyon = 250};

/// The triple to price with: the declared one when the chain carries [op_eip1559], the legacy
/// preset otherwise. Both the genesis pin (bcos-tool) and the engine call this, so the two can
/// never disagree about what "not declared" means.
[[nodiscard]] inline OpEip1559Params effectiveOpEip1559(
    std::optional<OpEip1559Params> const& declared) noexcept
{
    return declared.has_value() ? *declared : kLegacyOpEip1559Params;
}

}  // namespace bcos::engine
