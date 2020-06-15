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
#include <regex>

namespace dev
{
namespace precompiled
{
using namespace dev;
using namespace std;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// confidential payment
const char CONFIDENTIAL_PAYMENT_VERSION[] = "v0.2-generic";
const char CONFIDENTIAL_PAYMENT_REGEX_WHITELIST[] = "^v0.2-generic$";
// This is an empty blacklist.
const char CONFIDENTIAL_PAYMENT_REGEX_BLACKLIST[] = "^$";
const char API_CONFIDENTIAL_PAYMENT_IS_COMPATIBLE[] = "confidentialPaymentIsCompatible(string)";
const char API_CONFIDENTIAL_PAYMENT_GET_VERSION[] = "confidentialPaymentGetVersion()";
const char API_CONFIDENTIAL_PAYMENT_VERIFY_ISSUED_CREDIT[] =
    "confidentialPaymentVerifyIssuedCredit(string)";
const char API_CONFIDENTIAL_PAYMENT_VERIFY_FULFILLED_CREDIT[] =
    "confidentialPaymentVerifyFulfilledCredit(string)";
const char API_CONFIDENTIAL_PAYMENT_VERIFY_TRANSFERRED_CREDIT[] =
    "confidentialPaymentVerifyTransferredCredit(string)";
const char API_CONFIDENTIAL_PAYMENT_VERIFY_SPLIT_CREDIT[] =
    "confidentialPaymentVerifySplitCredit(string)";
const char API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TRANSFERRED_CREDIT[] =
    "confidentialPaymentVerifyMultiTransferredCredit(string,string)";
const char API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TSPLIT_CREDIT[] =
    "confidentialPaymentVerifyMultiSplitCredit(string,string)";

// anonymous voting
const char ANONYMOUS_VOTING_VERSION[] = "v0.2-generic";
const char ANONYMOUS_VOTING_REGEX_WHITELIST[] = "^v0.2-generic$";
const char ANONYMOUS_VOTING_REGEX_BLACKLIST[] = "^$";
const char API_ANONYMOUS_VOTING_IS_COMPATIBLE[] = "anonymousVotingIsCompatible(string)";
const char API_ANONYMOUS_VOTING_GET_VERSION[] = "anonymousVotingGetVersion()";
const char API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST[] =
    "anonymousVotingVerifyBoundedVoteRequest(string,string)";
const char API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST[] =
    "anonymousVotingVerifyUnboundedVoteRequest(string,string)";
const char API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST_UNLISTED[] =
    "anonymousVotingVerifyUnboundedVoteRequestUnlisted(string,string)";
const char API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE[] =
    "anonymousVotingAggregateVoteSumResponse(string,string,string)";
const char API_ANONYMOUS_VOTING_AGGREGATE_HPOINT[] =
    "anonymousVotingAggregateHPoint(string,string)";
const char API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST[] =
    "anonymousVotingVerifyCountRequest(string,string,string,string)";
const char API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM[] =
    "anonymousVotingAggregateDecryptedPartSum(string,string,string)";
const char API_ANONYMOUS_VOTING_VERIFY_VOTE_RESULT[] =
    "anonymousVotingVerifyVoteResult(string,string,string,string)";
const char API_ANONYMOUS_VOTING_GET_VOTE_RESULT_FROM_REQUEST[] =
    "anonymousVotingGetVoteResultFromRequest(string)";

// anonymous auction
const char ANONYMOUS_AUCTION_VERSION[] = "v0.2-generic";
const char ANONYMOUS_AUCTION_REGEX_WHITELIST[] = "^v0.2-generic$";
const char ANONYMOUS_AUCTION_REGEX_BLACKLIST[] = "^$";
const char API_ANONYMOUS_AUCTION_IS_COMPATIBLE[] = "anonymousAuctionIsCompatible(string)";
const char API_ANONYMOUS_AUCTION_GET_VERSION[] = "anonymousAuctionGetVersion()";
const char API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST[] =
    "anonymousAuctionVerifyBidSignatureFromBidRequest(string)";
const char API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST[] =
    "anonymousAuctionVerifyBidSignatureFromBidComparisonRequest(string)";
const char API_ANONYMOUS_AUCTION_VERIFY_WINNER[] =
    "anonymousAuctionVerifyWinner(string,string,string)";

const char WEDPR_VERFIY_FAILED[] = "verfiy failed";

const int WEDPR_SUCCESS = 0;
const int WEDPR_FAILURE = -1;

const char WEDPR_PRECOMPILED[] = "WedprPrecompiled";

WedprPrecompiled::WedprPrecompiled()
{
    // confidential payment
    if (confidential_payment_is_compatible(CONFIDENTIAL_PAYMENT_VERSION) == WEDPR_FAILURE)
    {
        string version = confidential_payment_get_version();
        logError(WEDPR_PRECOMPILED, "Confidential payment compatible error", "Node",
            CONFIDENTIAL_PAYMENT_VERSION, "Wedpr storage", version);
        throwException("Confidential payment compatible error");
    }
    name2Selector[API_CONFIDENTIAL_PAYMENT_IS_COMPATIBLE] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_IS_COMPATIBLE);
    name2Selector[API_CONFIDENTIAL_PAYMENT_GET_VERSION] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_GET_VERSION);
    name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_ISSUED_CREDIT] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_VERIFY_ISSUED_CREDIT);
    name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_FULFILLED_CREDIT] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_VERIFY_FULFILLED_CREDIT);
    name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_TRANSFERRED_CREDIT] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_VERIFY_TRANSFERRED_CREDIT);
    name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_SPLIT_CREDIT] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_VERIFY_SPLIT_CREDIT);
    name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TRANSFERRED_CREDIT] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TRANSFERRED_CREDIT);
    name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TSPLIT_CREDIT] =
        getFuncSelector(API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TSPLIT_CREDIT);

    // anonymous voting
    if (anonymous_voting_is_compatible(ANONYMOUS_VOTING_VERSION) == WEDPR_FAILURE)
    {
        string version = anonymous_voting_get_version();
        logError(WEDPR_PRECOMPILED, "Anonymous voting compatible error", "Node",
            ANONYMOUS_VOTING_VERSION, "Wedpr storage", version);
        throwException("Anonymous voting compatible error");
    }
    name2Selector[API_ANONYMOUS_VOTING_IS_COMPATIBLE] =
        getFuncSelector(API_ANONYMOUS_VOTING_IS_COMPATIBLE);
    name2Selector[API_ANONYMOUS_VOTING_GET_VERSION] =
        getFuncSelector(API_ANONYMOUS_VOTING_GET_VERSION);
    name2Selector[API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST);
    name2Selector[API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST_UNLISTED] =
        getFuncSelector(API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST_UNLISTED);
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
    name2Selector[API_ANONYMOUS_VOTING_VERIFY_VOTE_RESULT] =
        getFuncSelector(API_ANONYMOUS_VOTING_VERIFY_VOTE_RESULT);
    name2Selector[API_ANONYMOUS_VOTING_GET_VOTE_RESULT_FROM_REQUEST] =
        getFuncSelector(API_ANONYMOUS_VOTING_GET_VOTE_RESULT_FROM_REQUEST);

    // anonymous auction
    if (anonymous_auction_is_compatible(ANONYMOUS_AUCTION_VERSION) == WEDPR_FAILURE)
    {
        string version = anonymous_auction_get_version();
        logError(WEDPR_PRECOMPILED, "Anonymous auction compatible error", "Node",
            ANONYMOUS_AUCTION_VERSION, "Wedpr storage", version);
        throwException("Anonymous auction compatible error");
    }
    name2Selector[API_ANONYMOUS_AUCTION_IS_COMPATIBLE] =
        getFuncSelector(API_ANONYMOUS_AUCTION_IS_COMPATIBLE);
    name2Selector[API_ANONYMOUS_AUCTION_GET_VERSION] =
        getFuncSelector(API_ANONYMOUS_AUCTION_GET_VERSION);
    name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST] =
        getFuncSelector(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST);
    name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST] =
        getFuncSelector(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST);
    name2Selector[API_ANONYMOUS_AUCTION_VERIFY_WINNER] =
        getFuncSelector(API_ANONYMOUS_AUCTION_VERIFY_WINNER);
}

string WedprPrecompiled::toString()
{
    return WEDPR_PRECOMPILED;
}

PrecompiledExecResult::Ptr WedprPrecompiled::call(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef param, Address const& origin, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE(WEDPR_PRECOMPILED) << LOG_DESC("call")
                           << LOG_KV("param", toHex(param)) << LOG_KV("origin", origin)
                           << LOG_KV("context", context->txGasLimit());

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    callResult->gasPricer()->setMemUsed(param.size());

    // confidentialPaymentIsCompatible(string targetVersion)
    if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_IS_COMPATIBLE])
    {
        callResult->setExecResult(confidentialPaymentIsCompatible(abi, data));
    }
    // confidentialPaymentGetVersion()
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_GET_VERSION])
    {
        callResult->setExecResult(confidentialPaymentGetVersion(abi));
    }
    // confidentialPaymentVerifyIssuedCredit(string issueArgument)
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_ISSUED_CREDIT])
    {
        callResult->setExecResult(verifyIssuedCredit(abi, data));
    }
    // confidentialPaymentVerifyFulfilledCredit(string fulfillArgument)
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_FULFILLED_CREDIT])
    {
        callResult->setExecResult(verifyFulfilledCredit(abi, data));
    }
    // confidentialPaymentVerifyTransferredCredit(string transferRequest)
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_TRANSFERRED_CREDIT])
    {
        callResult->setExecResult(verifyTransferredCredit(abi, data));
    }
    // confidentialPaymentVerifySplitCredit(string splitRequest)
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_SPLIT_CREDIT])
    {
        callResult->setExecResult(verifySplitCredit(abi, data));
    }
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TRANSFERRED_CREDIT])
    {
        callResult->setExecResult(verifyMultiTransferredCredit(abi, data));
    }
    // confidentialPaymentVerifySplitCredit(string splitRequest)
    else if (func == name2Selector[API_CONFIDENTIAL_PAYMENT_VERIFY_MULTI_TSPLIT_CREDIT])
    {
        callResult->setExecResult(verifyMultiSplitCredit(abi, data));
    }

    // anonymousVotingIsCompatible(string targetVersion)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_IS_COMPATIBLE])
    {
        callResult->setExecResult(anonymousVotingIsCompatible(abi, data));
    }
    // anonymousVotingGetVersion()
    else if (func == name2Selector[API_ANONYMOUS_VOTING_GET_VERSION])
    {
        callResult->setExecResult(anonymousVotingGetVersion(abi));
    }
    // anonymousVotingVerifyBoundedVoteRequest(string systemParameters, string voteRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST])
    {
        callResult->setExecResult(verifyBoundedVoteRequest(abi, data));
    }
    // anonymousVotingVerifyUnboundedVoteRequest(string systemParameters, string voteRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST])
    {
        callResult->setExecResult(verifyUnboundedVoteRequest(abi, data));
    }
    // anonymousVotingVerifyUnboundedVoteRequestUnlisted(string systemParameters, string
    // voteRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST_UNLISTED])
    {
        callResult->setExecResult(verifyUnboundedVoteRequestUnlisted(abi, data));
    }
    // anonymousVotingAggregateVoteSumResponse(string systemParameters, string voteRequest, string
    // voteStorage)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE])
    {
        callResult->setExecResult(aggregateVoteSumResponse(abi, data));
    }
    // anonymousVotingAggregateHPoint(string hPointShare, string hPointSum)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_HPOINT])
    {
        callResult->setExecResult(aggregateHPoint(abi, data));
    }
    // anonymousVotingVerifyCountRequest(string systemParameters, string voteStorage, string
    // hPointShare, string decryptedRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST])
    {
        callResult->setExecResult(verifyCountRequest(abi, data));
    }
    // anonymousVotingAggregateDecryptedPartSum(string systemParameters, string decryptedRequest,
    // string decryptedResultPartStorage)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM])
    {
        callResult->setExecResult(aggregateDecryptedPartSum(abi, data));
    }
    // anonymousVotingVerifyVoteResult(string systemParameters, string voteStorageSum, string
    // decryptedResultPartStorageSum, string voteResultRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_VERIFY_VOTE_RESULT])
    {
        callResult->setExecResult(verifyVoteResult(abi, data));
    }
    // anonymousVotingGetVoteResultFromRequest(string voteResultRequest)
    else if (func == name2Selector[API_ANONYMOUS_VOTING_GET_VOTE_RESULT_FROM_REQUEST])
    {
        callResult->setExecResult(getVoteResultFromRequest(abi, data));
    }

    // anonymousAuctionIsCompatible(string targetVersion)
    else if (func == name2Selector[API_ANONYMOUS_AUCTION_IS_COMPATIBLE])
    {
        callResult->setExecResult(anonymousAuctionIsCompatible(abi, data));
    }
    // anonymousAuctionGetVersion()
    else if (func == name2Selector[API_ANONYMOUS_AUCTION_GET_VERSION])
    {
        callResult->setExecResult(anonymousAuctionGetVersion(abi));
    }
    // anonymousAuctionVerifyBidSignatureFromBidRequest(string bidRequest)
    else if (func == name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST])
    {
        callResult->setExecResult(verifyBidSignatureFromBidRequest(abi, data));
    }
    // anonymousAuctionVerifyBidSignatureFromBidComparisonRequest(string bidComparisonRequest)
    else if (func ==
             name2Selector[API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST])
    {
        callResult->setExecResult(VerifyBidSignatureFromBidComparisonRequest(abi, data));
    }
    // nonymousAuctionVerifyWinner(string winnerClaimRequest, string allBidStorageRequest)
    else if (func == name2Selector[API_ANONYMOUS_AUCTION_VERIFY_WINNER])
    {
        callResult->setExecResult(verifyWinner(abi, data));
    }
    // unknown function call
    else
    {
        logError(WEDPR_PRECOMPILED, "unknown func", "func", std::to_string(func));
        callResult->setExecResult(abi.abiIn("", u256(CODE_UNKNOW_FUNCTION_CALL)));
        throwException("unknown func");
    }
    return callResult;
}

bytes WedprPrecompiled::confidentialPaymentIsCompatible(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    // parse parameter
    string targetVersion;
    abi.abiOut(data, targetVersion);
    int result = isCompatible(
        targetVersion, CONFIDENTIAL_PAYMENT_REGEX_WHITELIST, CONFIDENTIAL_PAYMENT_REGEX_BLACKLIST);
    return abi.abiIn("", s256(result));
}

int WedprPrecompiled::isCompatible(
    const string& targetVersion, const char regexWhitelist[], const char regexBlacklist[])
{
    std::regex whitelist(regexWhitelist);
    std::regex blacklist(regexBlacklist);
    if (std::regex_match(targetVersion, whitelist) && !std::regex_match(targetVersion, blacklist))
    {
        return WEDPR_SUCCESS;
    }
    else
    {
        return WEDPR_FAILURE;
    }
}

bytes WedprPrecompiled::confidentialPaymentGetVersion(dev::eth::ContractABI& abi)
{
    return abi.abiIn("", string(CONFIDENTIAL_PAYMENT_VERSION));
}

bytes WedprPrecompiled::verifyIssuedCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    // parse parameter
    string issueArgument;
    abi.abiOut(data, issueArgument);

    // verify issued credit
    char* issueArgumentChar = stringToChar(issueArgument);
    if (verify_issued_credit(issueArgumentChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_issued_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_issued_credit failed");
    }

    string currentCredit = get_current_credit_by_issue_argument(issueArgumentChar);
    string creditStorage = get_storage_credit_by_issue_argument(issueArgumentChar);

    return abi.abiIn("", currentCredit, creditStorage);
}

bytes WedprPrecompiled::verifyFulfilledCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string fulfillArgument;
    abi.abiOut(data, fulfillArgument);
    char* fulfillArgumentChar = stringToChar(fulfillArgument);
    if (verify_fulfilled_credit(fulfillArgumentChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_fulfilled_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_fulfilled_credit failed");
    }

    string currentCredit = get_current_credit_by_fulfill_argument(fulfillArgumentChar);
    string creditStorage = get_credit_storage_by_fulfill_argument(fulfillArgumentChar);

    return abi.abiIn("", currentCredit, creditStorage);
}

bytes WedprPrecompiled::verifyTransferredCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string transferRequest;
    abi.abiOut(data, transferRequest);

    char* transferRequestChar = stringToChar(transferRequest);
    if (verify_transferred_credit(transferRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_transfer_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_transfer_credit failed");
    }

    string spentCurrentCredit = get_spent_current_credit_by_transfer_request(transferRequestChar);
    string spentCreditStorage = get_spent_credit_storage_by_transfer_request(transferRequestChar);
    string newCurrentCredit = get_new_current_credit_by_transfer_request(transferRequestChar);
    string newCreditStorage = get_new_credit_storage_by_transfer_request(transferRequestChar);

    return abi.abiIn(
        "", spentCurrentCredit, spentCreditStorage, newCurrentCredit, newCreditStorage);
}

bytes WedprPrecompiled::verifySplitCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string splitRequest;
    abi.abiOut(data, splitRequest);
    char* splitRequestChar = stringToChar(splitRequest);
    if (verify_split_credit(splitRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_split_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_split_credit failed");
    }

    string spentCurrentCredit = get_spent_current_credit_by_split_request(splitRequestChar);
    string spentCreditStorage = get_spent_credit_storage_by_split_request(splitRequestChar);
    string newCurrentCredit1 = get_new_current_credit1_by_split_request(splitRequestChar);
    string newCreditStorage1 = get_new_credit_storage1_by_split_request(splitRequestChar);
    string newCurrentCredit2 = get_new_current_credit2_by_split_request(splitRequestChar);
    string newCreditStorage2 = get_new_credit_storage2_by_split_request(splitRequestChar);

    return abi.abiIn("", spentCurrentCredit, spentCreditStorage, newCurrentCredit1,
        newCreditStorage1, newCurrentCredit2, newCreditStorage2);
}

bytes WedprPrecompiled::verifyMultiTransferredCredit(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string transferRequest;
    string combinedStoagre;
    abi.abiOut(data, combinedStoagre, transferRequest);

    char* transferRequestChar = stringToChar(transferRequest);
    char* combinedStoagreChar = stringToChar(combinedStoagre);
    if (verify_multi_transferred_credit(combinedStoagreChar, transferRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_multi_transfer_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_multi_transfer_credit failed");
    }

    string newCurrentCredit = get_new_current_credit_by_transfer_request(transferRequestChar);
    string newCreditStorage = get_new_credit_storage_by_transfer_request(transferRequestChar);

    return abi.abiIn("", newCurrentCredit, newCreditStorage);
}

bytes WedprPrecompiled::verifyMultiSplitCredit(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string splitRequest;
    string combinedStoagre;
    abi.abiOut(data, combinedStoagre, splitRequest);
    char* splitRequestChar = stringToChar(splitRequest);
    char* combinedStoagreChar = stringToChar(combinedStoagre);
    if (verify_multi_split_credit(combinedStoagreChar, splitRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_multi_split_credit", WEDPR_VERFIY_FAILED);
        throwException("verify_multi_split_credit failed");
    }

    string newCurrentCredit1 = get_new_current_credit1_by_split_request(splitRequestChar);
    string newCreditStorage1 = get_new_credit_storage1_by_split_request(splitRequestChar);
    string newCurrentCredit2 = get_new_current_credit2_by_split_request(splitRequestChar);
    string newCreditStorage2 = get_new_credit_storage2_by_split_request(splitRequestChar);

    return abi.abiIn(
        "", newCurrentCredit1, newCreditStorage1, newCurrentCredit2, newCreditStorage2);
}

bytes WedprPrecompiled::anonymousVotingIsCompatible(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    // parse parameter
    string targetVersion;
    abi.abiOut(data, targetVersion);
    int result = isCompatible(
        targetVersion, ANONYMOUS_VOTING_REGEX_WHITELIST, ANONYMOUS_VOTING_REGEX_BLACKLIST);
    return abi.abiIn("", s256(result));
}

bytes WedprPrecompiled::anonymousVotingGetVersion(dev::eth::ContractABI& abi)
{
    return abi.abiIn("", string(ANONYMOUS_VOTING_VERSION));
}

bytes WedprPrecompiled::verifyBoundedVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string voteRequest;
    abi.abiOut(data, systemParameters, voteRequest);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteRequestChar = stringToChar(voteRequest);
    if (verify_bounded_vote_request(systemParametersChar, voteRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_request", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_request failed");
    }

    string blankBallot = get_blank_ballot_from_vote_request(voteRequestChar);
    string voteStoragePart = get_vote_storage_from_vote_request(voteRequestChar);

    return abi.abiIn("", blankBallot, voteStoragePart);
}

bytes WedprPrecompiled::verifyUnboundedVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string voteRequest;
    abi.abiOut(data, systemParameters, voteRequest);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteRequestChar = stringToChar(voteRequest);
    if (verify_unbounded_vote_request(systemParametersChar, voteRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_request", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_request failed");
    }
    string voteStoragePart = get_vote_storage_from_vote_request(voteRequestChar);
    string blankBallot = get_blank_ballot_from_vote_request(voteRequestChar);

    return abi.abiIn("", blankBallot, voteStoragePart);
}

bytes WedprPrecompiled::verifyUnboundedVoteRequestUnlisted(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string voteRequest;
    abi.abiOut(data, systemParameters, voteRequest);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteRequestChar = stringToChar(voteRequest);
    if (verify_unbounded_vote_request_unlisted(systemParametersChar, voteRequestChar) !=
        WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_vote_request", WEDPR_VERFIY_FAILED);
        throwException("verify_vote_request failed");
    }
    string voteStoragePart = get_vote_storage_from_vote_request(voteRequestChar);
    string blankBallot = get_blank_ballot_from_vote_request(voteRequestChar);

    return abi.abiIn("", blankBallot, voteStoragePart);
}

bytes WedprPrecompiled::aggregateVoteSumResponse(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string voteStoragePart;
    string voteStorage;
    abi.abiOut(data, systemParameters, voteStoragePart, voteStorage);

    char* systemParametersChar = stringToChar(systemParameters);
    char* voteStorageChar = stringToChar(voteStorage);
    char* voteStoragePartChar = stringToChar(voteStoragePart);

    string voteStorageSum =
        aggregate_vote_sum_response(systemParametersChar, voteStoragePartChar, voteStorageChar);

    return abi.abiIn("", voteStorageSum);
}

bytes WedprPrecompiled::aggregateHPoint(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string hPointShare;
    string hPointSum;
    abi.abiOut(data, hPointShare, hPointSum);

    char* hPointShareChar = stringToChar(hPointShare);
    char* hPointSumChar = stringToChar(hPointSum);
    string newHPointSum = aggregate_h_point(hPointShareChar, hPointSumChar);

    return abi.abiIn("", newHPointSum);
}

bytes WedprPrecompiled::verifyCountRequest(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string voteStorage;
    string hPointShare;
    string decryptedRequest;
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
    string counterId = get_counter_id_from_decrypted_result_part_request(decryptedRequestChar);
    string decryptedResultPartStoragePart =
        get_decrypted_result_part_storage_from_decrypted_result_part_request(decryptedRequestChar);

    return abi.abiIn("", counterId, decryptedResultPartStoragePart);
}

bytes WedprPrecompiled::aggregateDecryptedPartSum(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string decryptedResultPartStoragePart;
    string decryptedResultPartStorage;
    abi.abiOut(data, systemParameters, decryptedResultPartStoragePart, decryptedResultPartStorage);
    char* systemParametersChar = stringToChar(systemParameters);
    char* decryptedResultPartStoragePartChar = stringToChar(decryptedResultPartStoragePart);
    char* decryptedResultPartStorageChar = stringToChar(decryptedResultPartStorage);

    string decryptedResultPartStorageSum = aggregate_decrypted_part_sum(
        systemParametersChar, decryptedResultPartStoragePartChar, decryptedResultPartStorageChar);

    return abi.abiIn("", decryptedResultPartStorageSum);
}

bytes WedprPrecompiled::verifyVoteResult(dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string systemParameters;
    string voteStorageSum;
    string decryptedResultPartStorageSum;
    string voteResultRequest;
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
    string voteResultRequest;
    abi.abiOut(data, voteResultRequest);
    char* voteResultRequestChar = stringToChar(voteResultRequest);
    string voteResultStorage = get_vote_result_from_request(voteResultRequestChar);

    return abi.abiIn("", voteResultStorage);
}

bytes WedprPrecompiled::anonymousAuctionIsCompatible(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    // parse parameter
    string targetVersion;
    abi.abiOut(data, targetVersion);
    int result = isCompatible(
        targetVersion, ANONYMOUS_AUCTION_REGEX_WHITELIST, ANONYMOUS_AUCTION_REGEX_BLACKLIST);
    return abi.abiIn("", s256(result));
}

bytes WedprPrecompiled::anonymousAuctionGetVersion(dev::eth::ContractABI& abi)
{
    return abi.abiIn("", string(ANONYMOUS_AUCTION_VERSION));
}

bytes WedprPrecompiled::verifyBidSignatureFromBidRequest(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string bidRequest;
    abi.abiOut(data, bidRequest);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("WedprPrecompiled") << LOG_KV("bidRequest", bidRequest);
    char* bidRequestChar = stringToChar(bidRequest);
    if (verify_bid_signature_from_bid_request(bidRequestChar) != WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_bid_signature_from_bid_request", WEDPR_VERFIY_FAILED);
        throwException("verify_bid_signature_from_bid_request failed");
    }
    string bidStorage = get_bid_storage_from_bid_request(bidRequestChar);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("WedprPrecompiled") << LOG_KV("bidStorage", bidStorage);
    return abi.abiIn("", bidStorage);
}
bytes WedprPrecompiled::VerifyBidSignatureFromBidComparisonRequest(
    dev::eth::ContractABI& abi, bytesConstRef& data)
{
    string bidComparisonRequest;
    abi.abiOut(data, bidComparisonRequest);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("WedprPrecompiled")
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
    string systemParameters;
    string winnerClaimRequest;
    string allBidRequest;
    abi.abiOut(data, systemParameters, winnerClaimRequest, allBidRequest);
    char* systemParametersChar = stringToChar(systemParameters);
    char* winnerClaimRequestChar = stringToChar(winnerClaimRequest);
    char* allBidRequestChar = stringToChar(allBidRequest);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("WedprPrecompiled")
                           << LOG_KV("systemParameters", systemParameters)
                           << LOG_KV("winnerClaimRequest", winnerClaimRequest)
                           << LOG_KV("allBidRequest", allBidRequest);
    if (verify_winner(systemParametersChar, winnerClaimRequestChar, allBidRequestChar) !=
        WEDPR_SUCCESS)
    {
        logError(WEDPR_PRECOMPILED, "verify_winner", WEDPR_VERFIY_FAILED);
        throwException("verify_winner failed");
    }
    int bidValue = get_bid_value_from_bid_winner_claim_request(winnerClaimRequestChar);
    string publicKey = get_public_key_from_bid_winner_claim_request(winnerClaimRequestChar);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("WedprPrecompiled") << LOG_KV("bidValue", bidValue)
                          << LOG_KV("publicKey", publicKey);
    return abi.abiIn("", bidValue, publicKey);
}

}  // namespace precompiled
}  // namespace dev
