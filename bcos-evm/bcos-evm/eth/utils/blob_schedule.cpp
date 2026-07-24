#include "blob_schedule.hpp"
#include "utils.hpp"
#include <string_view>

namespace evmone::test
{
state::BlobParams get_blob_params(evmc_revision rev)
{
    if (rev == EVMC_PRAGUE || rev == EVMC_EXPERIMENTAL)
        return {6, 9, 5007716};
    else if (rev > EVMC_PRAGUE)
        throw std::invalid_argument{
            "no hardcoded blob params for " + std::string{evmc::to_string(rev)}};
    else
        return {3, 6, 3338477};
}

state::BlobParams get_blob_params(evmc_revision rev, const BlobSchedule& blob_schedule)
{
    return get_blob_params(evmc::to_string(rev), blob_schedule, 0);
}

state::BlobParams get_blob_params(
    std::string_view network, const BlobSchedule& blob_schedule, int64_t timestamp)
{
    std::string fork;
    if (network == "PragueToOsakaAtTime15k")
        fork = timestamp >= 15'000 ? "Osaka" : "Prague";
    else if (network == "OsakaToBPO1AtTime15k")
        fork = timestamp >= 15'000 ? "BPO1" : "Osaka";
    else if (network == "BPO1ToBPO2AtTime15k")
        fork = timestamp >= 15'000 ? "BPO2" : "BPO1";
    else if (network == "BPO2ToBPO3AtTime15k")
        fork = timestamp >= 15'000 ? "BPO3" : "BPO2";
    else if (network == "BPO3ToBPO4AtTime15k")
        fork = timestamp >= 15'000 ? "BPO4" : "BPO3";
    else
        fork = network;
    if (const auto it = blob_schedule.find(fork); it != blob_schedule.end())
        return it->second;
    else
        return get_blob_params(to_rev_schedule(network).get_revision(timestamp));
}

}  // namespace evmone::test
