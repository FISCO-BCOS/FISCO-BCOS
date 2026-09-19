// OpDaMatrixSchemaTest.cpp — Task 2: da_matrix.json schema gate.
//
// Validates the DA/operator-fee parameter matrix grid (da-matrix/da_matrix.json)
// before any runner consumes it (Tasks 3-6). The assertions are deliberately
// grid-global — an EMPTY grid must fail (assertion 1 + 6), not pass vacuously.
// A schema that only checks "each case has its fields" is silent on an empty
// grid; the class-coverage assertions close that hole.
//
// Assertion set (mirrors task-2-brief.md Step 1):
//   1. cases non-empty;
//   2. each case carries id / slots{1,3,7,8} / envelope_ref / gas / block_time /
//      fork, and every slot value is exactly 32-byte hex (0x + 64 hex digits);
//   3. fork ∈ {ecotone,fjord,granite,holocene,isthmus,jovian,karst};
//   4. envelope_ref ∈ envelopes registry (and registry values are valid hex);
//   5. known_divergence, when present, ∈ {l1_fee_saturation,flz_zero_clamp,karst_alias};
//   6. 7-class coverage: id prefix ∈ {baseline,max,overflow,pre_isthmus,missing,
//      switch,blob}, each class present at least once;
//   7. a referenced envelope of 0-byte length without known_divergence==flz_zero_clamp
//      is a schema violation (guards a silent flz-0 clamp divergence).

#include <json/json.h>

#include <boost/test/unit_test.hpp>
#include <evmc/hex.hpp>

#include <cctype>
#include <fstream>
#include <sstream>
#include <set>
#include <string>

// Non-conflicting alias for jsoncpp's value type (same pattern as OpT8nReplayTest.cpp).
using JsonValue = Json::Value;

namespace
{
const std::set<std::string> kForks = {
    "ecotone", "fjord", "granite", "holocene", "isthmus", "jovian", "karst"};
const std::set<std::string> kDivergences = {
    "l1_fee_saturation", "flz_zero_clamp", "karst_alias"};
const std::set<std::string> kClassPrefixes = {
    "baseline", "blob", "max", "missing", "overflow", "pre_isthmus", "switch"};
const std::vector<std::string> kRequiredSlots = {"1", "3", "7", "8"};

Json::Value loadGrid()
{
    Json::Reader reader;
    Json::Value root;
    std::ifstream in(OP_DA_MATRIX_PATH);
    BOOST_REQUIRE_MESSAGE(in.good(), "cannot open da-matrix grid at " << OP_DA_MATRIX_PATH);
    std::stringstream ss;
    ss << in.rdbuf();
    BOOST_REQUIRE_MESSAGE(
        reader.parse(ss.str(), root), "JSON parse failed: " << reader.getFormattedErrorMessages());
    return root;
}

// Exactly 32 bytes: "0x" + 64 hex digits.
bool is32ByteHex(const std::string& s)
{
    if (s.size() != 66 || s.rfind("0x", 0) != 0)
        return false;
    for (size_t i = 2; i < s.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpDaMatrixSchema)

BOOST_AUTO_TEST_CASE(GridMeetsSchemaAndCoversSevenClasses)
{
    const Json::Value root = loadGrid();

    // Forward-compatible schema_version tag (non-fatal).
    BOOST_CHECK_MESSAGE(root.isMember("schema_version"), "grid missing schema_version");
    if (root.isMember("schema_version"))
        BOOST_CHECK_EQUAL(root["schema_version"].asInt(), 1);

    const Json::Value& cases = root["cases"];
    const Json::Value& envelopes = root["envelopes"];

    // 1. cases non-empty.
    BOOST_REQUIRE_MESSAGE(cases.isArray() && cases.size() > 0, "cases must be non-empty");

    // The envelope registry must exist and be non-empty (envelope_ref is a reference).
    BOOST_REQUIRE_MESSAGE(
        envelopes.isObject() && envelopes.size() > 0, "envelopes registry must be non-empty");

    std::set<std::string> seenClasses;
    for (const auto& c : cases)
    {
        const std::string id = c.get("id", "").asString();
        BOOST_REQUIRE_MESSAGE(!id.empty(), "case missing id");

        // 2. required fields present.
        for (const char* f : {"envelope_ref", "gas", "block_time", "fork"})
            BOOST_REQUIRE_MESSAGE(c.isMember(f), "case '" << id << "' missing field '" << f << "'");
        BOOST_REQUIRE_MESSAGE(c.isMember("slots"), "case '" << id << "' missing field 'slots'");

        // 2. slots: exactly {1,3,7,8}, each exactly 32-byte hex.
        const Json::Value& slots = c["slots"];
        BOOST_REQUIRE_MESSAGE(slots.isObject(), "case '" << id << "' slots must be an object");
        for (const auto& slot : kRequiredSlots)
        {
            BOOST_REQUIRE_MESSAGE(slots.isMember(slot), "case '" << id << "' missing slot '" << slot << "'");
            const std::string v = slots[slot].asString();
            BOOST_REQUIRE_MESSAGE(
                is32ByteHex(v), "case '" << id << "' slot '" << slot << "' must be exactly 32-byte hex, got: " << v);
        }

        // 3. fork enum.
        const std::string fork = c["fork"].asString();
        BOOST_REQUIRE_MESSAGE(
            kForks.count(fork) == 1, "case '" << id << "' fork '" << fork << "' not in fork enum");

        // 4. envelope_ref ∈ registry; registry value must be valid hex.
        const std::string envRef = c["envelope_ref"].asString();
        BOOST_REQUIRE_MESSAGE(
            envelopes.isMember(envRef), "case '" << id << "' envelope_ref '" << envRef << "' not in envelopes registry");
        const auto envBytes = evmc::from_hex(envelopes[envRef].asString());
        BOOST_REQUIRE_MESSAGE(
            envBytes.has_value(), "case '" << id << "' envelope '" << envRef << "' is not valid hex");

        // 5. known_divergence enum (null treated as absent).
        if (c.isMember("known_divergence") && !c["known_divergence"].isNull())
        {
            const std::string kd = c["known_divergence"].asString();
            BOOST_REQUIRE_MESSAGE(kDivergences.count(kd) == 1,
                "case '" << id << "' known_divergence '" << kd << "' not in divergence enum");
        }

        // 6. id must carry one of the 7 class prefixes.
        bool classed = false;
        for (const auto& p : kClassPrefixes)
        {
            if (id.rfind(p, 0) == 0)
            {
                seenClasses.insert(p);
                classed = true;
                break;
            }
        }
        BOOST_REQUIRE_MESSAGE(
            classed, "case '" << id << "' id must start with one of the 7 class prefixes (baseline/max/overflow/pre_isthmus/missing/switch/blob)");

        // 7. 0-byte envelope without flz_zero_clamp is a schema violation.
        if (envBytes->size() == 0)
        {
            const bool flzClamped =
                c.isMember("known_divergence") && c["known_divergence"].asString() == "flz_zero_clamp";
            BOOST_REQUIRE_MESSAGE(flzClamped,
                "case '" << id << "' references 0-byte envelope '" << envRef
                         << "' without known_divergence=flz_zero_clamp");
        }
    }

    // 6. every class present at least once.
    for (const auto& p : kClassPrefixes)
        BOOST_REQUIRE_MESSAGE(seenClasses.count(p) == 1,
            "grid missing class: no case with id prefix '" << p << "'");
}

BOOST_AUTO_TEST_SUITE_END()
