// run_fisco.cpp — Task 3: FISCO DA/operator-fee matrix runner.
//
// Reads the DA/operator-fee parameter grid (da-matrix/da_matrix.json), computes
// the L1 data fee and the charged operator fee for every case, and writes
// out_fisco.json as an array of {id, l1_cost, operator_cost}. All costs are
// emitted as lowercase "0x" hex, the op-geth hexutil.Big convention
// ("0x" + hex, no leading zeros).
//
// The operator cost MUST go through computeChargedOperatorCost — the
// has_operator_fee gate mirroring the block-transition decision
// (OpTransition.cpp:395) — never through raw computeOperatorCost, so a
// pre-Isthmus fork (fjord/granite/holocene) reports 0 even with a non-zero
// scalar/constant packed in slot 8.
//
// Error paths are deliberately EXCEPTION-FREE (review fix round 1/5): with the
// evmone/blst objects pulled in by this target's link configuration, an
// exception thrown from this TU does not unwind and aborts the process
// (SIGABRT / exit 134) instead of returning 1. Every failure is therefore an
// explicit check + std::cerr + return 1, never a throw.
//
// --check mode is Task 6; this runner only implements --grid/--out.

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/RollupCost.h>

#include <json/json.h>

#include <evmc/bytes.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <intx/intx.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace bcos::evm::opstack;

namespace
{
// Fork -> config resolution, shared by the --check mode (Task 6). Every fork in
// the da-matrix schema maps to exactly one of the seven OpForkSchedule configs.
// Throws on an unknown fork; callers MUST pre-check with isKnownFork() so this
// throw is unreachable from the (exception-free) runner path.
const OpForkConfig& forkConfigFor(const std::string& fork)
{
    if (fork == "ecotone")
        return ecotoneConfig();
    if (fork == "fjord")
        return fjordConfig();
    if (fork == "granite")
        return graniteConfig();
    if (fork == "holocene")
        return holoceneConfig();
    if (fork == "isthmus")
        return isthmusConfig();
    if (fork == "jovian")
        return jovianConfig();
    if (fork == "karst")
        return karstConfig();
    throw std::invalid_argument("unknown fork: " + fork);
}

bool isKnownFork(const std::string& fork)
{
    return fork == "ecotone" || fork == "fjord" || fork == "granite" || fork == "holocene" ||
           fork == "isthmus" || fork == "jovian" || fork == "karst";
}

// Decodes exactly-32-byte hex into `out`; returns false (no throw) on malformed input.
bool hexToBytes32(const std::string& s, evmc::bytes32& out)
{
    const auto v = evmc::from_hex<evmc::bytes32>(s);
    if (!v.has_value())
        return false;
    out = *v;
    return true;
}

// "0x" + lowercase hex, no leading zeros (op-geth hexutil.Big).
std::string toHex(const intx::uint256& v)
{
    return "0x" + intx::to_string(v, 16);
}
}  // namespace

int main(int argc, char** argv)
{
    std::string gridPath;
    std::string outPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--grid" && i + 1 < argc)
            gridPath = argv[++i];
        else if (arg == "--out" && i + 1 < argc)
            outPath = argv[++i];
        else
        {
            std::cerr << "usage: " << argv[0]
                      << " --grid <da_matrix.json> --out <out_fisco.json>\n";
            return 2;
        }
    }
    if (gridPath.empty() || outPath.empty())
    {
        std::cerr << "usage: " << argv[0] << " --grid <da_matrix.json> --out <out_fisco.json>\n";
        return 2;
    }

    // Load + parse the grid. No throw: a missing/unreadable file or a JSON
    // syntax error is a plain cerr + return 1.
    std::ifstream in(gridPath);
    if (!in.good())
    {
        std::cerr << "run_fisco: cannot open grid: " << gridPath << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ss.str(), root))
    {
        std::cerr << "run_fisco: JSON parse failed: " << reader.getFormattedErrorMessages() << "\n";
        return 1;
    }

    // Guard jsoncpp accessors: operator[] on a non-object Value throws Json::LogicError
    // (which does not unwind in this target) — check types before indexing.
    if (!root.isObject())
    {
        std::cerr << "run_fisco: grid must be a JSON object\n";
        return 1;
    }
    const Json::Value& envelopes = root["envelopes"];
    const Json::Value& cases = root["cases"];
    if (!envelopes.isObject() || !cases.isArray())
    {
        std::cerr << "run_fisco: grid must carry an 'envelopes' object and a 'cases' array\n";
        return 1;
    }

    Json::Value out(Json::arrayValue);
    for (const auto& c : cases)
    {
        if (!c.isObject())
        {
            std::cerr << "run_fisco: each case must be a JSON object\n";
            return 1;
        }
        const std::string id = c.get("id", "").asString();
        const std::string envRef = c.get("envelope_ref", "").asString();
        if (!envelopes.isMember(envRef))
        {
            std::cerr << "run_fisco: case '" << id << "' unknown envelope_ref: " << envRef << "\n";
            return 1;
        }

        // gas: jsoncpp asUInt64() — the overflow rows carry gas=2^64-1 and asDouble()
        // would round it up to 2^64 (off-by-one).
        const uint64_t gas = c["gas"].asUInt64();

        const auto envBytes = evmc::from_hex(envelopes[envRef].asString());
        if (!envBytes.has_value())
        {
            std::cerr << "run_fisco: case '" << id << "' envelope is not valid hex\n";
            return 1;
        }
        evmc::bytes_view env;
        if (!envBytes->empty())
            env = evmc::bytes_view{envBytes->data(), envBytes->size()};

        const Json::Value& slots = c["slots"];
        if (!slots.isObject())
        {
            std::cerr << "run_fisco: case '" << id << "' has no 'slots' object\n";
            return 1;
        }
        evmc::bytes32 slot1, slot3, slot7, slot8;
        if (!hexToBytes32(slots["1"].asString(), slot1) ||
            !hexToBytes32(slots["3"].asString(), slot3) ||
            !hexToBytes32(slots["7"].asString(), slot7) ||
            !hexToBytes32(slots["8"].asString(), slot8))
        {
            std::cerr << "run_fisco: case '" << id
                      << "' has an invalid slot (must be exactly 32-byte hex)\n";
            return 1;
        }

        const auto params = unpackOpFeeParams(slot1, slot3, slot7, slot8);

        const std::string fork = c["fork"].asString();
        if (!isKnownFork(fork))
        {
            std::cerr << "run_fisco: case '" << id << "' unknown fork: " << fork << "\n";
            return 1;
        }
        const OpForkConfig& cfg = forkConfigFor(fork);

        const auto l1 = computeL1Cost(params, env, cfg);
        const auto op = computeChargedOperatorCost(params, gas, cfg);

        Json::Value item(Json::objectValue);
        item["id"] = id;
        item["l1_cost"] = toHex(l1);
        item["operator_cost"] = toHex(op);
        out.append(item);

        std::cout << id << "\tl1=" << toHex(l1) << "\top=" << toHex(op) << "\n";
    }

    std::ofstream outFile(outPath);
    if (!outFile.good())
    {
        std::cerr << "run_fisco: cannot write output: " << outPath << "\n";
        return 1;
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    // StreamWriter::write returns int: 0 on success (jsoncpp writer.h:55).
    if (writer->write(out, &outFile) != 0)
    {
        std::cerr << "run_fisco: failed to serialize output\n";
        return 1;
    }
    outFile << "\n";
    return 0;
}
