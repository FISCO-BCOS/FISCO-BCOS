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
/** @file WedprPrecompiled.h
 *  @author caryliao
 *  @date 20190917
 */
#pragma once
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/Common.h>

namespace dev
{
namespace precompiled
{
// hidden asset
extern const char API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT[];
extern const char API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT[];
extern const char API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT[];
extern const char API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT[];

// anonymous voting
extern const char API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST[];
extern const char API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST[];
extern const char API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE[];
extern const char API_ANONYMOUS_VOTING_AGGREGATE_HPOINT[];
extern const char API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST[];
extern const char API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM[];
extern const char API_VERIFY_VOTE_RESULT[];
extern const char API_GET_VOTE_RESULT_FROM_REQUEST[];

// anonymous auction
extern const char API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE[];
extern const char API_ANONYMOUS_AUCTION_VERIFY_WINNER[];

extern const char WEDPR_VERFIY_FAILED[];
extern const char WEDPR_PRECOMPILED[];

extern const int WEDPR_SUCCESS;
extern const int WEDPR_FAILURE;

class WedprPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<WedprPrecompiled> Ptr;
    WedprPrecompiled();
    ~WedprPrecompiled(){};

    std::string toString() override;

    bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _param,
        Address const& _origin = Address()) override;

    bytes verifyIssuedCredit(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifyFulfilledCredit(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifyTransferredCredit(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifySplitCredit(dev::eth::ContractABI& abi, bytesConstRef& data);

    bytes verifyBoundedVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifyUnboundedVoteRequest(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes aggregateVoteSumResponse(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes aggregateHPoint(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifyCountRequest(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes aggregateDecryptedPartSum(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifyVoteResult(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes getVoteResultFromRequest(dev::eth::ContractABI& abi, bytesConstRef& data);

    bytes verifyBidSignature(dev::eth::ContractABI& abi, bytesConstRef& data);
    bytes verifyWinner(dev::eth::ContractABI& abi, bytesConstRef& data);
};

}  // namespace precompiled

}  // namespace dev
