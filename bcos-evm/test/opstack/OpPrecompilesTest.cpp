#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

using namespace bcos::evm::opstack;

namespace
{
/// The oracle stores op-revm's Rust `SpecId::<VARIANT>` names (uppercased) for the ETH base
/// fork each OP fork runs on. Map that name to the evmc revision our OpForkConfig::rev must
/// equal. Chosen over evmc::to_string because the latter spells "Cancun"/"Prague" (mixed
/// case) while the oracle key is the uppercase Rust variant — an explicit table keeps the
/// name/value mapping visible here rather than relying on two spellings happening to agree.
std::optional<evmc_revision> evmRevFromOracleSpecName(const std::string& name)
{
    static const std::map<std::string, evmc_revision> kByName{
        {"CANCUN", EVMC_CANCUN},
        {"PRAGUE", EVMC_PRAGUE},
        {"OSAKA", EVMC_OSAKA},
    };
    const auto it = kByName.find(name);
    if (it == kByName.end())
    {
        return std::nullopt;
    }
    return it->second;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpPrecompilesSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(P256VerifyGasOverrideAndAddressExtension, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const evmc::address addr{0x100};
    const auto& overrides = isthmusPrecompileOverrides();
    BOOST_CHECK(overrides.contains(addr));
    const auto* entry = overrides.find(addr);
    BOOST_REQUIRE((entry) != nullptr);
    BOOST_CHECK_EQUAL(entry->gas_cost_override, 3450);
    BOOST_CHECK_EQUAL(entry->max_input_size, 0U);
}

// clang-format off
BOOST_AUTO_TEST_CASE(Bn256PairingInputLimitNoGasOverride, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const evmc::address addr{0x08};
    const auto& overrides = isthmusPrecompileOverrides();
    BOOST_CHECK(overrides.contains(addr));
    const auto* entry = overrides.find(addr);
    BOOST_REQUIRE((entry) != nullptr);
    BOOST_CHECK_EQUAL(entry->max_input_size, 112687U);
    BOOST_CHECK_LT(entry->gas_cost_override, 0);
}

// clang-format off
BOOST_AUTO_TEST_CASE(BlsMsmPairingInputLimits, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto& overrides = isthmusPrecompileOverrides();

    const evmc::address g1msm{0x0c};
    BOOST_CHECK(overrides.contains(g1msm));
    const auto* g1Entry = overrides.find(g1msm);
    BOOST_REQUIRE((g1Entry) != nullptr);
    BOOST_CHECK_EQUAL(g1Entry->max_input_size, 513760U);
    BOOST_CHECK_LT(g1Entry->gas_cost_override, 0);

    const evmc::address g2msm{0x0e};
    BOOST_CHECK(overrides.contains(g2msm));
    const auto* g2Entry = overrides.find(g2msm);
    BOOST_REQUIRE((g2Entry) != nullptr);
    BOOST_CHECK_EQUAL(g2Entry->max_input_size, 488448U);
    BOOST_CHECK_LT(g2Entry->gas_cost_override, 0);

    const evmc::address pairing{0x0f};
    BOOST_CHECK(overrides.contains(pairing));
    const auto* pairingEntry = overrides.find(pairing);
    BOOST_REQUIRE((pairingEntry) != nullptr);
    BOOST_CHECK_EQUAL(pairingEntry->max_input_size, 235008U);
    BOOST_CHECK_LT(pairingEntry->gas_cost_override, 0);
}

// clang-format off
BOOST_AUTO_TEST_CASE(ForkConfigWiresPrecompiles, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK_EQUAL(isthmusConfig().precompiles, &isthmusPrecompileOverrides());
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianLimitsStricterThanIsthmus, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto* j08 = jovianPrecompileOverrides().find(evmc::address{0x08});
    const auto* i08 = isthmusPrecompileOverrides().find(evmc::address{0x08});
    BOOST_REQUIRE((j08) != nullptr);
    BOOST_REQUIRE((i08) != nullptr);
    BOOST_CHECK_EQUAL(j08->max_input_size, 81984u);
    BOOST_CHECK_LT(j08->max_input_size, i08->max_input_size);

    BOOST_CHECK_EQUAL(
        jovianPrecompileOverrides().find(evmc::address{0x0c})->max_input_size, 288960u);
    BOOST_CHECK_EQUAL(
        jovianPrecompileOverrides().find(evmc::address{0x0e})->max_input_size, 278784u);
    BOOST_CHECK_EQUAL(
        jovianPrecompileOverrides().find(evmc::address{0x0f})->max_input_size, 156672u);
    BOOST_CHECK_EQUAL(jovianConfig().precompiles, &jovianPrecompileOverrides());
    BOOST_CHECK_EQUAL(karstConfig().precompiles, &karstPrecompileOverrides());
    BOOST_CHECK(karstConfig().precompiles != jovianConfig().precompiles);
}

// Karst tightens bn256Pairing to 57600 (300 pairs; Jovian carried 81984 = 427 pairs) and
// pins P256VERIFY to the EIP-7951 pricing (6900) via the explicit 0x100 entry — the same
// facts the release line pinned through the vendored Osaka-gated fall-through, expressed
// here against the oracle-pinned override table (op-revm karst() swaps P256 explicitly).
// The BLS MSM/pairing limits carry over from Jovian unchanged.
BOOST_AUTO_TEST_CASE(KarstTightensBn256AndDropsP256Override)
{
    const auto& k = karstPrecompileOverrides();
    const auto* k08 = k.find(evmc::address{0x08});
    BOOST_REQUIRE((k08) != nullptr);
    BOOST_CHECK_EQUAL(k08->max_input_size, 57600u);
    BOOST_CHECK_EQUAL(k08->gas_cost_override, -1);
    BOOST_CHECK_LT(
        k08->max_input_size, jovianPrecompileOverrides().find(evmc::address{0x08})->max_input_size);

    const auto* kP256 = k.find(kP256VerifyAddress);
    BOOST_REQUIRE(kP256 != nullptr);
    BOOST_CHECK_EQUAL(kP256->gas_cost_override, 6900);
    BOOST_CHECK(jovianPrecompileOverrides().contains(kP256VerifyAddress));

    BOOST_CHECK_EQUAL(k.find(evmc::address{0x0c})->max_input_size, 288960u);
    BOOST_CHECK_EQUAL(k.find(evmc::address{0x0e})->max_input_size, 278784u);
    BOOST_CHECK_EQUAL(k.find(evmc::address{0x0f})->max_input_size, 156672u);

    BOOST_CHECK_EQUAL(karstConfig().precompiles, &karstPrecompileOverrides());
}

// Karst's precompile table judged by an EXTERNAL implementation (op-revm), not by its own
// literals. The oracle JSON is a tracked contract regenerated by
// tools/op-revm-oracle/extract.sh from the pinned checkout; the path is resolved from this
// translation unit so no CMake definition is needed.
// clang-format off
BOOST_AUTO_TEST_CASE(KarstTableMatchesOpRevmOracle, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto const oraclePath = std::filesystem::path(__FILE__).parent_path() / "op_revm_oracle.json";
    BOOST_REQUIRE_MESSAGE(std::filesystem::exists(oraclePath),
        "missing oracle contract at " << oraclePath.string()
                                      << " — regenerate with tools/op-revm-oracle/extract.sh");
    Json::Value oracle;
    {
        std::ifstream in(oraclePath);
        BOOST_REQUIRE_MESSAGE(
            Json::Reader{}.parse(in, oracle, false), "cannot parse " << oraclePath.string());
    }
    BOOST_TEST_MESSAGE("op-revm oracle revision: " << oracle["source"]["revision"].asString());

    const auto& table = karstPrecompileOverrides();
    const auto* bn254 = table.find(evmc::address{0x08});
    const auto* p256 = table.find(evmc::address{0x100});
    const auto* g1 = table.find(evmc::address{0x0c});
    const auto* g2 = table.find(evmc::address{0x0e});
    const auto* pair = table.find(evmc::address{0x0f});
    BOOST_REQUIRE(bn254 != nullptr);
    BOOST_REQUIRE(p256 != nullptr);
    BOOST_REQUIRE(g1 != nullptr);
    BOOST_REQUIRE(g2 != nullptr);
    BOOST_REQUIRE(pair != nullptr);

    BOOST_CHECK_EQUAL(bn254->max_input_size,
        static_cast<std::size_t>(oracle["bn254_pairing_max_input_size"].asUInt64()));
    BOOST_CHECK_EQUAL(g1->max_input_size,
        static_cast<std::size_t>(oracle["bls_g1_msm_max_input_size"].asUInt64()));
    BOOST_CHECK_EQUAL(g2->max_input_size,
        static_cast<std::size_t>(oracle["bls_g2_msm_max_input_size"].asUInt64()));
    BOOST_CHECK_EQUAL(pair->max_input_size,
        static_cast<std::size_t>(oracle["bls_pairing_max_input_size"].asUInt64()));
    // BLS caps are inherited from Jovian: op-revm's karst() clones jovian() and swaps only
    // modexp/P256/bn254-pair, so the Karst table must equal Jovian's for those three addresses.
    const auto* jovianG1 = jovianPrecompileOverrides().find(evmc::address{0x0c});
    const auto* jovianG2 = jovianPrecompileOverrides().find(evmc::address{0x0e});
    const auto* jovianPair = jovianPrecompileOverrides().find(evmc::address{0x0f});
    const auto* jovianBn254 = jovianPrecompileOverrides().find(evmc::address{0x08});
    BOOST_REQUIRE(jovianG1 != nullptr);
    BOOST_REQUIRE(jovianG2 != nullptr);
    BOOST_REQUIRE(jovianPair != nullptr);
    BOOST_REQUIRE(jovianBn254 != nullptr);
    BOOST_CHECK_EQUAL(jovianG1->max_input_size, g1->max_input_size);
    BOOST_CHECK_EQUAL(jovianG2->max_input_size, g2->max_input_size);
    BOOST_CHECK_EQUAL(jovianPair->max_input_size, pair->max_input_size);
    if (oracle["p256verify_gas"].isNull())
    {
        // Minimal read set recorded in the design doc: revm-precompile <version>/src/secp256r1.rs.
        BOOST_TEST_MESSAGE(
            "p256verify_gas not resolved from the local registry; minimal read set "
            "= revm-precompile "
            << oracle["revm_precompile_version"].asString() << "/src/secp256r1.rs");
    }
    else
    {
        BOOST_CHECK_EQUAL(
            p256->gas_cost_override, static_cast<int64_t>(oracle["p256verify_gas"].asInt64()));
    }
    // The bn254 pair must be the ONE address whose Karst value differs from Jovian's; if that
    // ever stops being true, the Karst row stopped being a real fork delta.
    BOOST_CHECK_LT(bn254->max_input_size, jovianBn254->max_input_size);
}

// Holocene/Jovian per-value anchors (WI-E5/E6), judged by the same EXTERNAL op-revm oracle:
// the ETH base fork each runs with, plus the two operator-fee numbers the formula is built from
// (exposed from RollupCost.h so the assertion pins production's constants, not a literal copy).
// clang-format off
BOOST_AUTO_TEST_CASE(HoloceneJovianAnchorsMatchOpRevmOracle, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto const oraclePath = std::filesystem::path(__FILE__).parent_path() / "op_revm_oracle.json";
    BOOST_REQUIRE_MESSAGE(std::filesystem::exists(oraclePath),
        "missing oracle contract at " << oraclePath.string()
                                      << " — regenerate with tools/op-revm-oracle/extract.sh");
    Json::Value oracle;
    {
        std::ifstream in(oraclePath);
        BOOST_REQUIRE_MESSAGE(
            Json::Reader{}.parse(in, oracle, false), "cannot parse " << oraclePath.string());
    }
    BOOST_TEST_MESSAGE("op-revm oracle revision: " << oracle["source"]["revision"].asString());

    // op-revm's spec.rs maps Holocene to the Cancun ETH spec and Jovian to Prague.
    const auto holoceneEthSpec = evmRevFromOracleSpecName(oracle["holocene_eth_spec"].asString());
    BOOST_REQUIRE_MESSAGE(holoceneEthSpec.has_value(),
        "unrecognized oracle key holocene_eth_spec=\"" << oracle["holocene_eth_spec"].asString()
                                                       << "\" — extend evmRevFromOracleSpecName");
    BOOST_CHECK_MESSAGE(holoceneConfig().rev == *holoceneEthSpec,
        "holoceneConfig().rev (evmc " << static_cast<int>(holoceneConfig().rev)
                                      << ") must equal oracle holocene_eth_spec=\""
                                      << oracle["holocene_eth_spec"].asString() << "\" (evmc "
                                      << static_cast<int>(*holoceneEthSpec) << ")");

    const auto jovianEthSpec = evmRevFromOracleSpecName(oracle["jovian_eth_spec"].asString());
    BOOST_REQUIRE_MESSAGE(jovianEthSpec.has_value(), "unrecognized oracle key jovian_eth_spec=\""
                                                         << oracle["jovian_eth_spec"].asString()
                                                         << "\" — extend evmRevFromOracleSpecName");
    BOOST_CHECK_MESSAGE(jovianConfig().rev == *jovianEthSpec,
        "jovianConfig().rev (evmc " << static_cast<int>(jovianConfig().rev)
                                    << ") must equal oracle jovian_eth_spec=\""
                                    << oracle["jovian_eth_spec"].asString() << "\" (evmc "
                                    << static_cast<int>(*jovianEthSpec) << ")");

    // Operator fee: Isthmus+ divides by the scalar's 1e6 decimal; Jovian multiplies it by 100.
    const auto divisor = oracle["operator_fee_scalar_decimal"].asInt64();
    BOOST_CHECK_MESSAGE(detail::c_operatorFeeScalarDivisor == divisor,
        "our operator-fee divisor (RollupCost.h detail::c_operatorFeeScalarDivisor="
            << detail::c_operatorFeeScalarDivisor << ") must equal oracle "
            << "operator_fee_scalar_decimal=" << divisor);
    const auto multiplier = oracle["operator_fee_jovian_multiplier"].asInt64();
    BOOST_CHECK_MESSAGE(detail::c_jovianOperatorFeeMultiplier == multiplier,
        "our Jovian operator-fee multiplier (RollupCost.h detail::c_jovianOperatorFeeMultiplier="
            << detail::c_jovianOperatorFeeMultiplier << ") must equal oracle "
            << "operator_fee_jovian_multiplier=" << multiplier);
}
BOOST_AUTO_TEST_SUITE_END()
