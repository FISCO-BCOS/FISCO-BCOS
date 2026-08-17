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

evmc::bytes32 hexToBytes32(const std::string& s)
{
    const auto v = evmc::from_hex<evmc::bytes32>(s);
    if (!v.has_value())
        throw std::invalid_argument("invalid 32-byte hex: " + s);
    return *v;
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

    try
    {
        Json::Value root;
        {
            std::ifstream in(gridPath);
            if (!in.good())
                throw std::runtime_error("cannot open grid: " + gridPath);
            std::stringstream ss;
            ss << in.rdbuf();
            Json::Reader reader;
            if (!reader.parse(ss.str(), root))
                throw std::runtime_error(
                    "JSON parse failed: " + reader.getFormattedErrorMessages());
        }

        const Json::Value& envelopes = root["envelopes"];
        const Json::Value& cases = root["cases"];
        if (!envelopes.isObject() || !cases.isArray())
            throw std::runtime_error("grid must carry an 'envelopes' object and a 'cases' array");

        Json::Value out(Json::arrayValue);
        for (const auto& c : cases)
        {
            const std::string id = c.get("id", "").asString();
            const std::string envRef = c.get("envelope_ref", "").asString();
            if (!envelopes.isMember(envRef))
                throw std::runtime_error("case '" + id + "' unknown envelope_ref: " + envRef);

            // gas: jsoncpp asUInt64() — the overflow rows carry gas=2^64-1 and asDouble()
            // would round it up to 2^64 (off-by-one).
            const uint64_t gas = c["gas"].asUInt64();

            const auto envBytes = evmc::from_hex(envelopes[envRef].asString());
            if (!envBytes.has_value())
                throw std::runtime_error("case '" + id + "' envelope is not valid hex");
            const evmc::bytes_view env = envBytes->empty() ?
                                             evmc::bytes_view{} :
                                             evmc::bytes_view{envBytes->data(), envBytes->size()};

            const Json::Value& slots = c["slots"];
            const auto slot1 = hexToBytes32(slots["1"].asString());
            const auto slot3 = hexToBytes32(slots["3"].asString());
            const auto slot7 = hexToBytes32(slots["7"].asString());
            const auto slot8 = hexToBytes32(slots["8"].asString());

            const auto params = unpackOpFeeParams(slot1, slot3, slot7, slot8);
            const OpForkConfig& cfg = forkConfigFor(c["fork"].asString());

            const auto l1 = computeL1Cost(params, env, cfg);
            const auto op = computeChargedOperatorCost(params, gas, cfg);

            Json::Value item(Json::objectValue);
            item["id"] = id;
            item["l1_cost"] = toHex(l1);
            item["operator_cost"] = toHex(op);
            out.append(item);

            std::cout << id << "\tl1=" << toHex(l1) << "\top=" << toHex(op) << "\n";
        }

        {
            std::ofstream outFile(outPath);
            if (!outFile.good())
                throw std::runtime_error("cannot write output: " + outPath);
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(out, &outFile);
            outFile << "\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "run_fisco: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
