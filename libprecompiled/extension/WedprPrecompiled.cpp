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
#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstorage/StorageException.h"
#include "wedpr-generated/ffi_storage.h"
#include <libdevcore/Common.h>
#include <libdevcore/Log.h>
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

// hidden payment
const char API_HIDDEN_PAYMENT_VERIFY_ISSUED_CREDIT[] = "hiddenPaymentVerifyIssuedCredit(string)";
const char API_HIDDEN_PAYMENT_VERIFY_FULFILLED_CREDIT[] =
    "hiddenPaymentVerifyFulfilledCredit(string)";
const char API_HIDDEN_PAYMENT_VERIFY_TRANSFERRED_CREDIT[] =
    "hiddenPaymentVerifyTransferredCredit(string)";
const char API_HIDDEN_PAYMENT_VERIFY_SPLIT_CREDIT[] = "hiddenPaymentVerifySplitCredit(string)";

// anonymous voting
const char API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST[] =
    "anonymousVotingVerifyBoundedVoteRequest(string,string)";
// anonymous voting
const char API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST[] =
    "anonymousVotingVerifyUnboundedVoteRequest(string,string)";
const char API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE[] =
    "anonymousVotingAggregateVoteSumResponse(string,string,string)";
const char API_ANONYMOUS_VOTING_AGGREGATE_HPOINT[] =
    "anonymousVotingAggregateHPoint(string,string)";
const char API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST[] =
    "anonymousVotingVerifyCountRequest(string,string,string,string)";
const char API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM[] =
    "anonymousVotingAggregateDecryptedPartSum(string,string,string)";
const char API_VERIFY_VOTE_RESULT[] =
    "anonymousVotingVerifyVoteResult(string,string,string,string)";
const char API_GET_VOTE_RESULT_FROM_REQUEST[] = "anonymousVotingGetVoteResultFromRequest(string)";

// anonymous auction
const char API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST[] =
    "anonymousAuctionVerifyBidSignatureFromBidRequest(string)";
const char API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST[] =
    "anonymousAuctionVerifyBidSignatureFromBidComparisonRequest(string)";
const char API_ANONYMOUS_AUCTION_VERIFY_WINNER[] = "anonymousAuctionVerifyWinner(string,string)";

const char WEDPR_VERFIY_FAILED[] = "verfiy failed";

const int WEDPR_SUCCESS = 0;
const int WEDPR_FAILURE = -1;

const char WEDPR_PRECOMPILED[] = "WedprPrecompiled";

WedprPrecompiled::WedprPrecompiled()
{
    // hidden payment
    name2Selector[API_HIDDEN_PAYMENT_VERIFY_ISSUED_CREDIT] =
        getFuncSelector(API_HIDDEN_PAYMENT_VERIFY_ISSUED_CREDIT);
    name2Selector[API_HIDDEN_PAYMENT_VERIFY_FULFILLED_CREDIT] =
        getFuncSelector(API_HIDDEN_PAYMENT_VERIFY_FULFILLED_CREDIT);
    name2Selector[API_HIDDEN_PAYMENT_VERIFY_TRANSFERRED_CREDIT] =
        getFuncSelector(API_HIDDEN_PAYMENT_VERIFY_TRANSFERRED_CREDIT);
    name2Selector[API_HIDDEN_PAYMENT_VERIFY_SPLIT_CREDIT] =
        getFuncSelector(API_HIDDEN_PAYMENT_VERIFY_SPLIT_CREDIT);

    // anonymous voting
    name2Selector[API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST);
    name2Selector[API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST);
    name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE] =
        getFuncSelector(API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE);
    name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_HPOINT] =
        getFuncSelector(API_ANONYMOUS_VOTING_AGGREGATE_HPOINT);
    name2Selector[API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST);
    name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM] =
        getFuncSelector(API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM);
    name2Selector[API_VERIFY_VOTE_RESULT] = getFuncSelector(API_VERIFY_VOTE_RESULT);
    name2Selector[API_GET_VOTE_RESULT_FROM_REQUEST] =
        getFuncSelector(API_GET_VOTE_RESULT_FROM_REQUEST);

    // anonymous auction
    name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST] =
        getFuncSelector(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST);
    name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST] =
        getFuncSelector(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST);
    name2Selector[API_ANONYMOUS_AUCTION_VERIFY_WINNER] =
        getFuncSelector(API_ANONYMOUS_AUCTION_VERIFY_WINNER);
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

    // hiddenPaymentVerifyIssuedCredit(string issueArgument)
    if (func == name2Selector[API_HIDDEN_PAYMENT_VERIFY_ISSUED_CREDIT])
    {
        out = verifyIssuedCredit(abi, data);
    }
    // hiddenPaymentVerifyFulfilledCredit(string fulfillArgument)
    else if (func == name2Selector[API_HIDDEN_PAYMENT_VERIFY_FULFILLED_CREDIT])
    {
        out = verifyFulfilledCredit(abi, data);
    }
    // hiddenPaymentVerifyTransferredCredit(string transferRequest)
    else if (func == name2Selector[API_HIDDEN_PAYMENT_VERIFY_TRANSFERRED_CREDIT])
    {
        out = verifyTransferredCredit(abi, data);
    }
    // hiddenPaymentVerifySplitCredit(string splitRequest)
    else if (func == name2Selector[API_HIDDEN_PAYMENT_VERIFY_SPLIT_CREDIT])
    {
        out = verifySplitCredit(abi, data);
    }
    // anonymousVotingVerifyBoundedVoteRequest(string systemParameters, string voteRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST])
    {
        out = verifyBoundedVoteRequest(abi, data);
    }
    // anonymousVotingVerifyUnboundedVoteRequest(string systemParameters, string voteRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST])
    {
        out = verifyUnboundedVoteRequest(abi, data);
    }
    // anonymousVotingAggregateVoteSumResponse(string systemParameters, string voteRequest, string
    // voteStorage)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE])
    {
        out = aggregateVoteSumResponse(abi, data);
    }
    // anonymousVotingAggregateHPoint(string hPointShare, string hPointSum)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_HPOINT])
    {
        out = aggregateHPoint(abi, data);
    }
    // anonymousVotingVerifyCountRequest(string systemParameters, string voteStorage, string
    // hPointShare, string decryptedRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST])
    {
        out = verifyCountRequest(abi, data);
    }
    // anonymousVotingAggregateDecryptedPartSum(string systemParameters, string decryptedRequest,
    // string decryptedResultPartStorage)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM])
    {
        out = aggregateDecryptedPartSum(abi, data);
    }
    else if (func == name2Selector[API_VERIFY_VOTE_RESULT])
    {
        out = verifyVoteResult(abi, data);
    }
    else if (func == name2Selector[API_GET_VOTE_RESULT_FROM_REQUEST])
    {
        out = getVoteResultFromRequest(abi, data);
    }
    else if (func == name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST])
    {
        out = verifyBidSignatureFromBidRequest(abi, data);
    }
    else if (func ==
             name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST])
    {
        out = VerifyBidSignatureFromBidComparisonRequest(abi, data);
    }
    else if (func == name2Selector[API_ANONYMOUS_AUCTION_VERIFY_WINNER])
    {
        out = verifyWinner(abi, data);
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
    // parse parameter
    std::string issueArgument;
    abi.abiOut(data, issueArgument);

    // verify issued credit
    char* issueArgumentChar = stringToChar(issueArgument);
    if (verify_issued_credit(issueArgumentChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_issued_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_issued_credit failed");
    }

    std::string currentCredit = get_current_credit_by_issue_argument(issueArgumentChar);
    std::string creditStorage = get_storage_credit_by_issue_argument(issueArgumentChar);

    return abi.abiIn("", currentCredit, creditStorage);
}

bytes WedprPrecompiled::verifyFulfilledCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string fulfillArgument;
    abi.abiOut(data, fulfillArgument);
    char* fulfillArgumentChar = stringToChar(fulfillArgument);
    if (verify_fulfilled_credit(fulfillArgumentChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_fulfilled_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_fulfilled_credit failed");
    }

    std::string currentCredit = get_current_credit_by_fulfill_argument(fulfillArgumentChar);
    std::string creditStorage = get_credit_storage_by_fulfill_argument(fulfillArgumentChar);

    return abi.abiIn("", currentCredit, creditStorage);
}

bytes WedprPrecompiled::verifyTransferredCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string transferRequest;
    abi.abiOut(data, transferRequest);

    char* transferRequestChar = stringToChar(transferRequest);
    if (verify_transferred_credit(transferRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_transfer_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_transfer_credit failed");
    }

    std::string spentCurrentCredit =
        get_spent_current_credit_by_transfer_request(transferRequestChar);
    std::string spentCreditStorage =
        get_spent_credit_storage_by_transfer_request(transferRequestChar);
    std::string newCurrentCredit = get_new_current_credit_by_transfer_request(transferRequestChar);
    std::string newCreditStorage = get_new_credit_storage_by_transfer_request(transferRequestChar);

    return abi.abiIn(
        "", spentCurrentCredit, spentCreditStorage, newCurrentCredit, newCreditStorage);
}

bytes WedprPrecompiled::verifySplitCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string splitRequest;
    abi.abiOut(data, splitRequest);
    char* splitRequestChar = stringToChar(splitRequest);
    if (verify_split_credit(splitRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_split_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_split_credit failed");
    }

    std::string spentCurrentCredit = get_spent_current_credit_by_split_request(splitRequestChar);
    std::string spentCreditStorage = get_spent_credit_storage_by_split_request(splitRequestChar);
    std::string newCurrentCredit1 = get_new_current_credit1_by_split_request(splitRequestChar);
    std::string newCreditStorage1 = get_new_credit_storage1_by_split_request(splitRequestChar);
    std::string newCurrentCredit2 = get_new_current_credit2_by_split_request(splitRequestChar);
    std::string newCreditStorage2 = get_new_credit_storage2_by_split_request(splitRequestChar);

    return abi.abiIn("", spentCurrentCredit, spentCreditStorage, newCurrentCredit1,
        newCreditStorage1, newCurrentCredit2, newCreditStorage2);
}

bytes WedprPrecompiled::verifyBoundedVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteRequest;
    abi.abiOut(data, systemParameters, voteRequest);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteRequestChar = stringToChar(voteRequest);
    if (verify_bounded_vote_request(systemParametersChar, voteRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_request", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_request failed");
    }

    std::string blankBallot = get_blank_ballot_from_vote_request(voteRequestChar);
    std::string voteStoragePart = get_vote_storage_from_vote_request(voteRequestChar);

    return abi.abiIn("", blankBallot, voteStoragePart);
}

bytes WedprPrecompiled::verifyUnboundedVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteRequest;
    abi.abiOut(data, systemParameters, voteRequest);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteRequestChar = stringToChar(voteRequest);
    if (verify_unbounded_vote_request(systemParametersChar, voteRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_request", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_request failed");
    }
    std::string voteStoragePart = get_vote_storage_from_vote_request(voteRequestChar);
    std::string blankBallot = get_blank_ballot_from_vote_request(voteRequestChar);

    return abi.abiIn("", blankBallot, voteStoragePart);
}

bytes WedprPrecompiled::aggregateVoteSumResponse(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteStoragePart;
    std::string voteStorage;
    abi.abiOut(data, systemParameters, voteStoragePart, voteStorage);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteStorageChar = stringToChar(voteStorage);
    char* voteStoragePartChar = stringToChar(voteStoragePart);

    std::string voteStorageSum =
        aggregate_vote_sum_response(systemParametersChar, voteStoragePartChar, voteStorageChar);

    return abi.abiIn("", voteStorageSum);
}

bytes WedprPrecompiled::aggregateHPoint(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string hPointShare;
    std::string hPointSum;
    abi.abiOut(data, hPointShare, hPointSum);

    char* hPointShareChar = stringToChar(hPointShare);
    char* hPointSumChar = stringToChar(hPointSum);
    std::string newHPointSum = aggregate_h_point(hPointShareChar, hPointSumChar);

    return abi.abiIn("", newHPointSum);
}

bytes WedprPrecompiled::verifyCountRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteStorage;
    std::string hPointShare;
    std::string decryptedRequest;
    abi.abiOut(data, systemParameters, voteStorage, hPointShare, decryptedRequest);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteStorageChar = stringToChar(voteStorage);
    char* hPointShareChar = stringToChar(hPointShare);
    char* decryptedRequestChar = stringToChar(decryptedRequest);
    if (verify_count_request(systemParametersChar, voteStorageChar, hPointShareChar,
            decryptedRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_count_request", WEDPR_VERFIY_FAILED);
        throwException("verify_count_request failed");
    }
    std::string counterId = get_counter_id_from_decrypted_result_part_request(decryptedRequestChar);
    std::string decryptedResultPartStoragePart =
        get_decrypted_result_part_storage_from_decrypted_result_part_request(decryptedRequestChar);

    return abi.abiIn("", counterId, decryptedResultPartStoragePart);
}
bytes WedprPrecompiled::aggregateDecryptedPartSum(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string decryptedResultPartStoragePart;
    std::string decryptedResultPartStorage;
    abi.abiOut(data, systemParameters, decryptedResultPartStoragePart, decryptedResultPartStorage);
    char* systemParametersChar = stringToChar(systemParameters);
    char* decryptedResultPartStoragePartChar = stringToChar(decryptedResultPartStoragePart);
    char* decryptedResultPartStorageChar = stringToChar(decryptedResultPartStorage);

    std::string decryptedResultPartStorageSum = aggregate_decrypted_part_sum(
        systemParametersChar, decryptedResultPartStoragePartChar, decryptedResultPartStorageChar);

    return abi.abiIn("", decryptedResultPartStorageSum);
}
bytes WedprPrecompiled::verifyVoteResult(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string systemParameters;
    std::string voteStorageSum;
    std::string decryptedResultPartStorageSum;
    std::string voteResultRequest;
    abi.abiOut(
        data, systemParameters, voteStorageSum, decryptedResultPartStorageSum, voteResultRequest);
    char* systemParametersChar = stringToChar(systemParameters);
    char* voteStorageSumChar = stringToChar(voteStorageSum);
    char* decryptedResultPartStorageSumChar = stringToChar(decryptedResultPartStorageSum);
    char* voteResultRequestChar = stringToChar(voteResultRequest);

    if (verify_vote_result(systemParametersChar, voteStorageSumChar,
            decryptedResultPartStorageSumChar, voteResultRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_result", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_result failed");
    }

    return abi.abiIn("", WEDPR_SUCCESS);
}
bytes WedprPrecompiled::getVoteResultFromRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string voteResultRequest;
    abi.abiOut(data, voteResultRequest);
    char* voteResultRequestChar = stringToChar(voteResultRequest);
    std::string voteResultStorage = get_vote_result_from_request(voteResultRequestChar);

    return abi.abiIn("", voteResultStorage);
}
bytes WedprPrecompiled::verifyBidSignatureFromBidRequest(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string bidRequest;
    abi.abiOut(data, bidRequest);
    std::cout << "###bidRequest" << bidRequest << std::endl;
    PRECOMPILED_LOG(INFO) << LOG_BADGE("WedprPrecompiled") << LOG_KV("bidRequest", bidRequest);
    char* bidRequestChar = stringToChar(bidRequest);
    if (verify_bid_signature_from_bid_request(bidRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_bid_signature_from_bid_request", WEDPR_VERFIY_FAILED);
        throwException("verify_bid_signature_from_bid_request failed");
    }
    std::string bidStorage = get_bid_storage_from_bid_request(bidRequestChar);
    std::cout << "###bidStorage" << bidStorage << std::endl;
    PRECOMPILED_LOG(INFO) << LOG_BADGE("WedprPrecompiled") << LOG_KV("bidStorage", bidStorage);
    return abi.abiIn("", bidStorage);
}
bytes WedprPrecompiled::VerifyBidSignatureFromBidComparisonRequest(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string bidComparisonRequest;
    abi.abiOut(data, bidComparisonRequest);
    std::cout << "###bidComparisonRequest" << bidComparisonRequest << std::endl;
    PRECOMPILED_LOG(INFO) << LOG_BADGE("WedprPrecompiled")
                          << LOG_KV("bidComparisonRequest", bidComparisonRequest);
    char* bidComparisonRequestChar = stringToChar(bidComparisonRequest);
    if (verify_bid_signature_from_bid_comparison_request(bidComparisonRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_bid_signature_from_bid_comparison_request",
            WEDPR_VERFIY_FAILED);
        throwException("verify_bid_signature_from_bid_comparison_request failed");
    }
    return abi.abiIn("", WEDPR_SUCCESS);
}
bytes WedprPrecompiled::verifyWinner(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    std::string winnerClaimRequest;
    std::string allBidRequest;
    abi.abiOut(data, winnerClaimRequest, allBidRequest);
    char* winnerClaimRequestChar = stringToChar(winnerClaimRequest);
    char* allBidRequestChar = stringToChar(allBidRequest);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("WedprPrecompiled")
                          << LOG_KV("winnerClaimRequest", winnerClaimRequest)
                          << LOG_KV("allBidRequest", allBidRequest);
    if (verify_winner(winnerClaimRequestChar, allBidRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_winner", WEDPR_VERFIY_FAILED);
        throwException("verify_winner failed");
    }
    int bidValue = get_bid_value_from_bid_winner_claim_request(winnerClaimRequestChar);
    std::string publicKey = get_public_key_from_bid_winner_claim_request(winnerClaimRequestChar);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("WedprPrecompiled") << LOG_KV("bidValue", bidValue)
                          << LOG_KV("publicKey", publicKey);
    return abi.abiIn("", bidValue, publicKey);
}

}  // namespace precompiled
}  // namespace dev
