/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file WedprPrecompiled.cpp
 *  @author caryliao
 *  @date 20190917
 */

#include "WedprPrecompiled.h"
#include "libprecompiled/Common.h"
#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/StorageException.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include "wedpr-generated/ffi_anonymous_voting.h"
#include "wedpr-generated/ffi_hidden_asset.h"
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>

namespace dev
{
namespace precompiled
{
using namespace dev;
using namespace std;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// hidden asset
const char API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT[] = "hiddenAssetVerifyIssuedCredit(bytes)";
const char API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT[] = "hiddenAssetVerifyFulfilledCredit(bytes)";
const char API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT[] =
    "hiddenAssetVerifyTransferredCredit(bytes)";
const char API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT[] = "hiddenAssetVerifySplitCredit(bytes)";

// anonymous voting
const char API_ANONYMOUS_VOTING_VERIFY_VOTE_REQUEST[] =
    "anonymousVotingVerifyVoteRequest(bytes,bytes)";
const char API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE[] =
    "anonymousVotingAggregateVoteSumResponse(bytes,bytes,bytes)";
const char API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST[] =
    "anonymousVotingVerifyCountRequest(bytes,bytes,string,bytes)";
const char API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM[] =
    "anonymousVotingAggregateDecryptedPartSum(bytes,bytes,bytes)";
const char API_ANONYMOUS_VOTING_COUNTS_CANDIDATES_RESULT[] =
    "anonymousVotingCountsCandidatesResult(bytes,bytes,bytes)";


const char WEDPR_VERFIY_FAILED[] = "verfiy failed";

const int WEDPR_SUCCESS = 0;
const int WEDPR_FAILURE = -1;

const char WEDPR_PRECOMPILED[] = "WedprPrecompiled";

WedprPrecompiled::WedprPrecompiled()
{
    // hidden asset
    name2Selector[API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT);
    name2Selector[API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT);
    name2Selector[API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT);
    name2Selector[API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT);

    // anonymous voting
    name2Selector[API_ANONYMOUS_VOTING_VERIFY_VOTE_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_VERIFY_VOTE_REQUEST);
    name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE] =
        getFuncSelector(API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE);
    name2Selector[API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST);
    name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM] =
        getFuncSelector(API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM);
    name2Selector[API_ANONYMOUS_VOTING_COUNTS_CANDIDATES_RESULT] =
        getFuncSelector(API_ANONYMOUS_VOTING_COUNTS_CANDIDATES_RESULT);
}

std::string WedprPrecompiled::toString()
{
    return WEDPR_PRECOMPILED;
}

bytes WedprPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE(WEDPR_PRECOMPILED) << LOG_DESC("call")
                           << LOG_KV("param", toHex(param)) << LOG_KV("origin", origin)
                           << LOG_KV("context", context->txGasLimit());

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    // hiddenAssetVerifyIssuedCredit(bytes issue_argument_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT])
    {
        out = verifyIssuedCredit(abi, data);
    }
    // hiddenAssetVerifyFulfilledCredit(bytes fulfill_argument_pb)
    else if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT])
    {
        out = verifyFulfilledCredit(abi, data);
    }
    // hiddenAssetVerifyTransferredCredit(bytes transfer_request_pb)
    else if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT])
    {
        out = verifyTransferredCredit(abi, data);
    }
    // hiddenAssetVerifySplitCredit(bytes split_request_pb)
    else if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT])
    {
        out = verifySplitCredit(abi, data);
    }
    // anonymousVotingVerifyVoteRequest(bytes systemParameters, bytes voteRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_VERIFY_VOTE_REQUEST])
    {
        out = verifyVoteRequest(abi, data);
    }
    // anonymousVotingAggregateVoteSumResponse(bytes systemParameters, bytes voteRequest, bytes
    // voteStorage)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE])
    {
        out = aggregateVoteSumResponse(abi, data);
    }
    // anonymousVotingVerifyCountRequest(bytes systemParameters, bytes voteStorage, string
    // hPointShare, bytes decryptedRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST])
    {
        out = verifyCountRequest(abi, data);
    }
    // anonymousVotingAggregateDecryptedPartSum(bytes systemParameters, bytes decryptRequest, bytes
    // decryptedResultPartStorage)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM])
    {
        out = aggregateDecryptedPartSum(abi, data);
    }
    // anonymousVotingCountsCandidatesResult(bytes systemParameters, bytes voteStorage, bytes
    // voteSumTotal)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_COUNTS_CANDIDATES_RESULT])
    {
        out = countingCandidatesResult(abi, data);
    }
    else
    {
        // unknown function call
        logError(WEDPR_PRECOMPILED, "unknown func", "func", std::to_string(func));
        throwException("unknown func");
    }
    return out;
}

bytes WedprPrecompiled::verifyIssuedCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    return abi.abiIn("", "", "");
}

bytes WedprPrecompiled::verifyFulfilledCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    return abi.abiIn("", "", "");
}

bytes WedprPrecompiled::verifyTransferredCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    return abi.abiIn("", "", "");
}

bytes WedprPrecompiled::verifySplitCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    return abi.abiIn("", "", "");
}

bytes WedprPrecompiled::verifyVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteRequest;
    abi.abiOut(data, systemParameters, voteRequest);

    char* systemParametersChar = string_to_char(systemParameters);
    char* voteRequestChar = string_to_char(voteRequest);
    if (verify_vote_request(systemParametersChar, voteRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_request", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_request failed");
    }

    return abi.abiIn("", WEDPR_SUCCESS);
}

bytes WedprPrecompiled::aggregateVoteSumResponse(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteRequest;
    std::string voteStorage;
    abi.abiOut(data, systemParameters, voteRequest, voteStorage);

    char* systemParametersChar = string_to_char(systemParameters);
    char* voteRequestChar = string_to_char(voteRequest);
    char* voteStorageChar = string_to_char(voteStorage);

    char* voteStoragePartChar = get_vote_storage_from_vote_request(voteRequestChar);
    std::string blankBallot = get_blank_ballot_from_vote_storage(voteStoragePartChar);
    std::string voteStoragePart = voteStoragePartChar;
    std::string voteStorageSum =
        aggregate_vote_sum_response(systemParametersChar, voteRequestChar, voteStorageChar);

    return abi.abiIn("", blankBallot, voteStoragePart, voteStorageSum);
}

bytes WedprPrecompiled::verifyCountRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteStorage;
    std::string hPointShare;
    std::string decryptedRequest;
    abi.abiOut(data, systemParameters, voteStorage, hPointShare, decryptedRequest);

    char* systemParametersChar = string_to_char(systemParameters);
    char* voteStorageChar = string_to_char(voteStorage);
    char* hPointShareChar = string_to_char(hPointShare);
    char* decryptedRequestChar = string_to_char(decryptedRequest);
    if (verify_count_request(systemParametersChar, voteStorageChar, hPointShareChar,
            decryptedRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_count_request", WEDPR_VERFIY_FAILED);
        throwException("verify_count_request failed");
    }

    return abi.abiIn("", WEDPR_SUCCESS);
}
bytes WedprPrecompiled::aggregateDecryptedPartSum(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string decryptRequest;
    std::string decryptedResultPartStorage;
    abi.abiOut(data, systemParameters, decryptRequest, decryptedResultPartStorage);

    char* systemParametersChar = string_to_char(systemParameters);
    char* decryptRequestChar = string_to_char(decryptRequest);
    char* decryptedResultPartStorageChar = string_to_char(decryptedResultPartStorage);

    std::string counterId = get_counter_id_from_decrypted_result_part_request(decryptRequestChar);
    std::string decryptedResultPartStoragePart =
        get_decrypted_result_part_storage_from_decrypted_result_part_request(decryptRequestChar);
    std::string decryptedResultPartStorageSum = aggregate_decrypted_part_sum(
        systemParametersChar, decryptRequestChar, decryptedResultPartStorageChar);

    return abi.abiIn("", counterId, decryptedResultPartStoragePart, decryptedResultPartStorageSum);
}
bytes WedprPrecompiled::countingCandidatesResult(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteStorage;
    std::string voteSumTotal;
    abi.abiOut(data, systemParameters, voteStorage, voteSumTotal);

    char* systemParametersChar = string_to_char(systemParameters);
    char* voteStorageChar = string_to_char(voteStorage);
    char* voteSumTotalChar = string_to_char(voteSumTotal);
    std::string countingResult =
        counting_candidates_result(systemParametersChar, voteStorageChar, voteSumTotalChar);

    return abi.abiIn("", countingResult);
}
}  // namespace precompiled
}  // namespace dev
