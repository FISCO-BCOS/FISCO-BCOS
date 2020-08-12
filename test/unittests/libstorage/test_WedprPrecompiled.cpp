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
/** @file test_WedprPrecompiled.cpp
 *  @author caryliao
 *  @date 20191009
 */

#include "../libstorage/MemoryStorage.h"
#include "Common.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libprecompiled/extension/WedprPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_WedprPrecompiled
{
class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};
};
struct WedprPrecompiledFixture
{
    WedprPrecompiledFixture()
    {
        context = std::make_shared<ExecutiveContext>();
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context->setBlockInfo(blockInfo);

        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        ExecutiveContextFactory factory;
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();

        tableFactoryPrecompiled = std::make_shared<dev::precompiled::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        wedprPrecompiled = context->getPrecompiled(Address(0x5018));
        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        wedprPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~WedprPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr wedprPrecompiled;
    dev::precompiled::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(WedprPrecompiled, WedprPrecompiledFixture)

BOOST_AUTO_TEST_CASE(confidentialPaymentVerifyIssuedCredit)
{
    dev::eth::ContractABI abi;
    std::string issueArgument =
        "CqkCCogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYRzFIMytrSHJxbk96WU"
        "cxR0xqeUQ1Ym9oUkNXajRSS2NXTEdoL1RBOERNenRhRzlrSUlGYktFcTh5WXZXOVdKRFByMW1SQ055azR1TSs0UXF1"
        "QlJiWnhzPRIINWViMzZmOGIaSAosYm03TmlQM1o2NUk3MXhXblpSdlNFWFJSWnh3aVhCcFBFRzFMQnFQa0kycz0SGF"
        "Z6eG9leDJMVGFhUUNrMlpOdUFvK2c9PSJICixibTdOaVAzWjY1STcxeFduWlJ2U0VYUlJaeHdpWEJwUEVHMUxCcVBr"
        "STJzPRIYVnp4b2V4MkxUYWFRQ2syWk51QW8rZz09EixQR0kyKzU1eXNTYS92UDlhb3M5YXA1Um81N2krRHIzcXJCeF"
        "JvYUd1Sm1nPRoDCIAI";
    bytes param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_ISSUED_CREDIT, issueArgument);
    // confidentialPaymentVerifyIssuedCredit(string issueArgument)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string currentCredit;
    std::string creditStorage;

    abi.abiOut(&out, currentCredit, creditStorage);
    BOOST_TEST(currentCredit ==
               "CixibTdOaVAzWjY1STcxeFduWlJ2U0VYUlJaeHdpWEJwUEVHMUxCcVBrSTJzPRIYVnp4b2V4MkxUYWFRQ2s"
               "yWk51QW8rZz09");
    BOOST_TEST(creditStorage ==
               "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYRzFIMytrSHJxbk9"
               "6WUcxR0xqeUQ1Ym9oUkNXajRSS2NXTEdoL1RBOERNenRhRzlrSUlGYktFcTh5WXZXOVdKRFByMW1SQ055az"
               "R1TSs0UXF1QlJiWnhzPRIINWViMzZmOGIaSAosYm03TmlQM1o2NUk3MXhXblpSdlNFWFJSWnh3aVhCcFBFR"
               "zFMQnFQa0kycz0SGFZ6eG9leDJMVGFhUUNrMlpOdUFvK2c9PSJICixibTdOaVAzWjY1STcxeFduWlJ2U0VY"
               "UlJaeHdpWEJwUEVHMUxCcVBrSTJzPRIYVnp4b2V4MkxUYWFRQ2syWk51QW8rZz09");

    std::string errorIssueArgument = "123";
    param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_ISSUED_CREDIT, errorIssueArgument);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(confidentialPaymentVerifyFulfilledCredit)
{
    dev::eth::ContractABI abi;
    std::string fulfillArgument =
        "CqkCCogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYRzhhcGRRVTdPSmRtMk"
        "I3bGdQMVhGZTFQcTJiQWUwUklydW1RN2oxc2VJaTljNGxraURwRTU0aStINXduSlBET0VNbStuS1RZd2IrN21iL0ZP"
        "R1JzWjlJPRIINWViMzZhMTkaSAosUkk0QW9XQjhFeW9wYlVOOTZCdjc2STQvRU05NUNkOVc2bU9YY0J6RE9XMD0SGF"
        "hEeEg0dHBVUllHT29id3g5d1YwTUE9PSJICiwzc0pqOFlYR2ZjTU9PNW56OTZCbkhqR1YyRG5VYTdjaXlSQmdBaXJt"
        "S3dFPRIYdkxIaW5reE1Ubk9ETGY2YjNGdGN3Zz09Eo8BL0xoUXgrRC9zQmhSSlFZVzJwLzc4U0FGY0dkUTJ2SmRkUU"
        "9qaC80dUpRST18eXRIVWhPMUdlMzFQV1Q2eXhRaElLNjd0UU9FQ0xLZXJXem1veTc1U3h3az18MGRZU0NGUmZTNFFr"
        "MlQvUzUxUjNFM2xldHdVTkQ5d09kTVFBNW90aEZRbz18NWViMzZhMTk=";
    bytes param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_FULFILLED_CREDIT, fulfillArgument);
    // confidentialPaymentVerifyFulfilledCredit(string fulfillArgument)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string currentCredit;
    std::string creditStorage;

    abi.abiOut(&out, currentCredit, creditStorage);
    BOOST_TEST(currentCredit ==
               "CixSSTRBb1dCOEV5b3BiVU45NkJ2NzZJNC9FTTk1Q2Q5VzZtT1hjQnpET1cwPRIYWER4SDR0cFVSWUdPb2J"
               "3eDl3VjBNQT09");
    BOOST_TEST(creditStorage ==
               "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYRzhhcGRRVTdPSmR"
               "tMkI3bGdQMVhGZTFQcTJiQWUwUklydW1RN2oxc2VJaTljNGxraURwRTU0aStINXduSlBET0VNbStuS1RZd2"
               "IrN21iL0ZPR1JzWjlJPRIINWViMzZhMTkaSAosUkk0QW9XQjhFeW9wYlVOOTZCdjc2STQvRU05NUNkOVc2b"
               "U9YY0J6RE9XMD0SGFhEeEg0dHBVUllHT29id3g5d1YwTUE9PSJICiwzc0pqOFlYR2ZjTU9PNW56OTZCbkhq"
               "R1YyRG5VYTdjaXlSQmdBaXJtS3dFPRIYdkxIaW5reE1Ubk9ETGY2YjNGdGN3Zz09");

    std::string errorFulfillArgument = "123";
    param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_FULFILLED_CREDIT, errorFulfillArgument);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(confidentialPaymentVerifyTransferredCredit)
{
    dev::eth::ContractABI abi;
    std::string transferRequest =
        "CqkCCogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSENwdjlFTUZYdERqOH"
        "ZVODJ5VlRacXpyblYwQURidXRzUnVlM1VMK0pldDJTZlpjM0JaTndRVkhYOFBuZkxkMjFUZXFydEc3STh2ZGhvNllp"
        "c3d6SEYwPRIINWViMzZhZTUaSAosY2ltWHVuQ0dGalVaeDRaK3dpTVFmWW45dTRicjN1YktDOW5lNTRVcWZIOD0SGE"
        "F5bkp4N3FyU1pLclcxWnhqVjlBdHc9PSJICixjaW1YdW5DR0ZqVVp4NFord2lNUWZZbjl1NGJyM3ViS0M5bmU1NFVx"
        "Zkg4PRIYQXluSng3cXJTWktyVzFaeGpWOUF0dz09EqkCCogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRG"
        "JYK01pY3FNTDU5MWVDMhJYSENwdjlFTUZYdERqOHZVODJ5VlRacXpyblYwQURidXRzUnVlM1VMK0pldDJTZlpjM0Ja"
        "TndRVkhYOFBuZkxkMjFUZXFydEc3STh2ZGhvNllpc3d6SEYwPRIINWViMzZhZTUaSAosSHB3S1RIc0ZyNWRwODJ6Vz"
        "dJQndBUk5ONUFseHZVV2Z1M291TUgzM0xtRT0SGDFCQURqSytsUmdLWkZrdk1aMEN2Nnc9PSJICixjaW1YdW5DR0Zq"
        "VVp4NFord2lNUWZZbjl1NGJyM3ViS0M5bmU1NFVxZkg4PRIYQXluSng3cXJTWktyVzFaeGpWOUF0dz09GvgCCixpdm"
        "JVVUQ5TklBKzJvOUZGUE5xOTR3cHk4VnQ0MjByOWJJLy8vZm05SHk4PRIsV3M2aW0ycEUxa282YlN4NlVKTGo4OGp1"
        "K0JpTzE2ejdrdm1VTzVCMjQyND0aLHcyK0Z6TWRibzJtOEUxUzZZQUVXZ05qQmZBUDRLMEZJUXN0M3RucXFlUWM9Ii"
        "xLSVN3aWpRc2haZEdtVThoTHR0UklMa2REVmZqcXJNTFBBK2kzbmZObXc4PSosdzNPRnpNZGJvMm04RTFTNllBRVdn"
        "TmpCZkFQNEswRklRc3QzdG5xcWVRYz06jwF1Q0RyZjA2dm82cElpb0IxVHVhR1cxU3ZKczF3cHBQOUU0WXFxOTRPZV"
        "FvPXw2RFNzUFR4b09OQnhmc3ZQVmwzcHgwdzcycXAwcEQweHpYWndXZ1lCRVE0PXxyc3RHSjQyWkhuV2tvWTNKWUtp"
        "TWRhMkQ2M20yb2ZtcXBVMlN4aUE3aVFVPXw1ZWIzNmFlNSIIdHJhbnNmZXI=";
    bytes param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_TRANSFERRED_CREDIT, transferRequest);
    // confidentialPaymentVerifyTransferredCredit(string transferRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string spentCurrentCredit;
    std::string spentCreditStorage;
    std::string newCurrentCredit;
    std::string newCreditStorage;

    abi.abiOut(&out, spentCurrentCredit, spentCreditStorage, newCurrentCredit, newCreditStorage);
    BOOST_TEST(spentCurrentCredit ==
               "CixjaW1YdW5DR0ZqVVp4NFord2lNUWZZbjl1NGJyM3ViS0M5bmU1NFVxZkg4PRIYQXluSng3cXJTWktyVzF"
               "aeGpWOUF0dz09");
    BOOST_TEST(spentCreditStorage ==
               "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSENwdjlFTUZYdER"
               "qOHZVODJ5VlRacXpyblYwQURidXRzUnVlM1VMK0pldDJTZlpjM0JaTndRVkhYOFBuZkxkMjFUZXFydEc3ST"
               "h2ZGhvNllpc3d6SEYwPRIINWViMzZhZTUaSAosY2ltWHVuQ0dGalVaeDRaK3dpTVFmWW45dTRicjN1YktDO"
               "W5lNTRVcWZIOD0SGEF5bkp4N3FyU1pLclcxWnhqVjlBdHc9PSJICixjaW1YdW5DR0ZqVVp4NFord2lNUWZZ"
               "bjl1NGJyM3ViS0M5bmU1NFVxZkg4PRIYQXluSng3cXJTWktyVzFaeGpWOUF0dz09");
    BOOST_TEST(newCurrentCredit ==
               "CixIcHdLVEhzRnI1ZHA4MnpXN0lCd0FSTk41QWx4dlVXZnUzb3VNSDMzTG1FPRIYMUJBRGpLK2xSZ0taRmt"
               "2TVowQ3Y2dz09");
    BOOST_TEST(newCreditStorage ==
               "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSENwdjlFTUZYdER"
               "qOHZVODJ5VlRacXpyblYwQURidXRzUnVlM1VMK0pldDJTZlpjM0JaTndRVkhYOFBuZkxkMjFUZXFydEc3ST"
               "h2ZGhvNllpc3d6SEYwPRIINWViMzZhZTUaSAosSHB3S1RIc0ZyNWRwODJ6VzdJQndBUk5ONUFseHZVV2Z1M"
               "291TUgzM0xtRT0SGDFCQURqSytsUmdLWkZrdk1aMEN2Nnc9PSJICixjaW1YdW5DR0ZqVVp4NFord2lNUWZZ"
               "bjl1NGJyM3ViS0M5bmU1NFVxZkg4PRIYQXluSng3cXJTWktyVzFaeGpWOUF0dz09");

    std::string errorTransferRequest = "123";
    param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_TRANSFERRED_CREDIT, errorTransferRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

// BOOST_AUTO_TEST_CASE(confidentialPaymentVerifySplitCredit)
// {
//     dev::eth::ContractABI abi;
//     std::string splitRequest =
//         "CqkCCogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSEM3L1NKbVFxc0krcl"
//         "BxZEY5cGJNejU5TTYrVTl0NmI3aEVvSEJ6ZXgzbllXZ3N4ZndFYWtBN0JvMWRodzY1a01lNVN1ZTRIdzVzTHRRNHBF"
//         "QmhQaXJBPRIINWViMzZiODUaSAosQWxvRzQ4WWZXTFZXS0U0MnJKUzdSWU5oc25NTEdFemhTeXpiekVWQldHMD0SGD"
//         "dGNDcvOHZuVHhTWWplMUd3R2Z0WWc9PSJICixBbG9HNDhZZldMVldLRTQyckpTN1JZTmhzbk1MR0V6aFN5emJ6RVZC"
//         "V0cwPRIYN0Y0Ny84dm5UeFNZamUxR3dHZnRZZz09EqkCCogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRG"
//         "JYK01pY3FNTDU5MWVDMhJYSEM3L1NKbVFxc0krclBxZEY5cGJNejU5TTYrVTl0NmI3aEVvSEJ6ZXgzbllXZ3N4ZndF"
//         "YWtBN0JvMWRodzY1a01lNVN1ZTRIdzVzTHRRNHBFQmhQaXJBPRIINWViMzZiODUaSAosMVBDOWRycjdXNk54U3k2K2"
//         "5zbGRmQzlUUFRNdStyS2I1K2lLQk5ubndnRT0SGGFWZ2pPcEdiUVYyYnhFSkR3MklmU1E9PSJICixBbG9HNDhZZldM"
//         "VldLRTQyckpTN1JZTmhzbk1MR0V6aFN5emJ6RVZCV0cwPRIYN0Y0Ny84dm5UeFNZamUxR3dHZnRZZz09EqkCCogBCi"
//         "xBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSEM3L1NKbVFxc0krclBxZEY5cGJN"
//         "ejU5TTYrVTl0NmI3aEVvSEJ6ZXgzbllXZ3N4ZndFYWtBN0JvMWRodzY1a01lNVN1ZTRIdzVzTHRRNHBFQmhQaXJBPR"
//         "IINWViMzZiODUaSAosNUVaRWQwUDI4cEM2U2xjNWdpekNvbUljdU81WENFVnEyc25xb1cxdkFGVT0SGE0wY2RicERU"
//         "UjlhbVZQTDlRbkhvTEE9PSJICixBbG9HNDhZZldMVldLRTQyckpTN1JZTmhzbk1MR0V6aFN5emJ6RVZCV0cwPRIYN0"
//         "Y0Ny84dm5UeFNZamUxR3dHZnRZZz09Go8QCvcHCix5bnkyc0FrWmg0Mzdvajl2djZDQjBIUm9qUkhKYUIyR2tzY1d4"
//         "TVNWU3hnPRIsU2dENkg2Y1BvUk03ZzB1STJiZE9yc0hhMFU2Q3NtYnBGNjQ0MWE0cmRnRT0iLDJyRS9NNW9lUUJ2RU"
//         "tnVStGUkRNeWlleVhHUFF5cUJzZHlybzY0UExQUUU9MtgFZ0c3K0I1Z1V0VWtLbEJ3N3NuVk9DRlU1cVdWN2dtNXJw"
//         "QUtqOHppVUlSejBTcmY5bXg5RWxwQ21HVGZudFJQUUVPWnFORUVDSmpyd3VUY3BGRjlpS0lEVE9PZmhlRzBYcG9FS0"
//         "pTclEzcWxXekNUbUlab2tZcGxxL2tHcWdyZGlUc1p1djR5ZGxacFhGR1BacjNVVk0vZjVJOU8yeTA0dm9xT2lKdzNp"
//         "NUd5UEozUDY4emViZzBaWU8raG1oRkNVdFJVZlg4eWhnSmNsbTAzdTNUbkpES0NRM3NYU1RJS0VYWHVxeG9wYnlDM2"
//         "c1d2NnTitYTy9GMWVIUjVCQzlBRDhHd044UWduZFVTdWZoekJSUGRjcUdNaWNVbVhCNzdlVkdaRUEvcHJNUXU0SUhl"
//         "czRxWUFsYy9ZeU5oRnljamlJRjU1NDJ0OFAxYTdzWkI4bkVJd05pelRidy9uNDRpMnk3b3UyUVJJV3FzOWlqcm93cm"
//         "R4NUpRSlltcUZuaWdVaWdNaXd4eTZqY3RQWXI5RkdMYUdZcHRNU2dqY2FLYmR1L2tlVkI2dytFRGVMSjhzTjZzdmJu"
//         "MVgxb3cwQlprZmZMYkhxZDE1bkRLczZnNWJvdnl3SmlKeGFnUXEvWW9xNmR1aExNKzJ2WFl0NGVaZStBUExNMFVpUG"
//         "RHTDB6VnI5QkhvUDlxWWN4UW9Sdi9hNzNsdUphSEhsMkhQYUx4Uk9uTmV0ZmNNVnh6bVlpYjBmVEJBT0p0RUJZTGxB"
//         "VW5CdDU2YS9kSnV1dU5uL0Uvb0NBaWZiL0N1RFNtZmdzWUh3SnRzc1JJRDFVUmZwOFpzL3JCQ2ptYTlpTUVOaGVrRV"
//         "VyL1ZOY1JNQWlraUYrbm4vVzNVOUhuTGpyM1dCQlhXelZ2Y3pHV3I3QTliM0k0YU1BWnRINm85WmY5NVhqQjdWeWRI"
//         "cStlMmpGWlZ4RTJDTEV4YkRnPT06jwE0dmlnNkR6bnJ2ZHFIUWtHb1VZQ0tQcHp5MC94MVcxQUFNemF4WEtvZXdNPX"
//         "xLTWhPZ2ZkY3g1YU45bTArbnBQQUd5cjl5NDU2L09GZnlPKzRlU2lQNlFzPXx2aVloU29ZajNNYW14eXh6ZnA2eGtR"
//         "MXY3ZXlSWGkwWTRUc3VkNGhqNncwPXw1ZWIzNmI4NRKLCAoscnJJaTE3b0NYNzJEcW9TeUFWWkpoSnNtVDNxSjRiZi"
//         "9GUlRmaklXQzdtQT0SLGdEZlRsbTZ1dVNBY2Q0Z0ZGUVM0bTFhc1BJbFdxL0tTeVVXYWhMcjI5RkU9GixFbUxMcmJH"
//         "MHpoMEswalNSNitGdU5lMW1JdzRabTdyczYyc1B1eWg0ZmdBPSIsOWVqVkcrNFVyUTRSUkpXbFVxRnZ4Rmtja2VRYn"
//         "JvMzRTRitEWVphWTdBND0qLGRtTExyYkcwemgwSzBqU1I2K0Z1TmUxbUl3NFptN3JzNjJzUHV5aDRmZ0E9MtgFbHRL"
//         "a1VpU1pzMEFncnhBNUc1WmtQOVRiSkFKV2hlcFYyNGcralJtVHNuUXczM0hRMWRzM2l6bmdEV2piNlBpM2pLZWQ5M1"
//         "pWTFhRbjkxcXJxUzYzYkVybjBhTG8yY3RUWThjRlpGOCsyS21mQmVnTDZKWEJBUjZyMW5Bc2ZKczE4clFKQTlKS1hU"
//         "S0JUUmkyZ01RRi9FV3FHVjlFLzlmTG1LOUpOQXFkWVc3TGZNUkgwZ29INTUxNk1iaHR1dWlmeXdrWTVZUFhtM1NHTH"
//         "hPTUhKMFdBdjN2NTZLbjZjTVNSd2JMaVBYdFgzek9Xc0lZK2h2MklMRmY4UUR1bjZRSE5jYmZrdjdUWTBuOWxJT2d6"
//         "RG9zMG5UWDlBN2dKdHorZ0VJc3pmR2tUQVB3aERGNVEvb0E4a3pxRDBLQndJZlpOa3pJL3R6Y2ZKY2ZiNlluWis1Vk"
//         "cwenN1LzlCeDRjSEIxaXlJajM0eHJnOW8rYVNFeUxxNEFPZmsvTFBidEJvbnZFY0c2ZXpDVis3QUdhZHQ3KzRDTThs"
//         "WUlsamZRSVlLcG55NHY2QytSejg2R29pcmlVZEFNTmxsZmdKSUVFSnlvYjRMTFVveXIzTUx4ang1cjZtWFBSQ3pCUj"
//         "JDN1lQUFB2TWxTU1pHY0FHWUM0SkdqZUZaak1pOTc3ZUh6bDdzcHY5OHNLZVpCUnFPbENUeGJOQjQ2cjN0VXVjZjU3"
//         "ZjRtMzBFdVZCMTFsU3oxelRrc21aNXVrTnZuRFNWYW8rcS9NQXFPbGhUZE03VEhBL2MrU2pmVGJjdWZTRGNtanhYWH"
//         "F0TldmTEQ1S0NsaWxTSDhWaW5XSmtZWS9TZmhNVXBtWWo4TWhhbUF0V1dLK3dpZk54ai9yNFBjcTRuV2g0TEFFc1ZY"
//         "cWloZzEzUm5sUjRZd0ZjYnJTKzR2VGptZEgrOXp5QnBIdlZmcVBmaGFLYngvZEJ3PT1CSAosMVBDOWRycjdXNk54U3"
//         "k2K25zbGRmQzlUUFRNdStyS2I1K2lLQk5ubndnRT0SGGFWZ2pPcEdiUVYyYnhFSkR3MklmU1E9PRoFc3BsaXQ=";
//     bytes param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_SPLIT_CREDIT, splitRequest);
//     // confidentialPaymentVerifySplitCredit(string splitRequest)
//     bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

//     std::string spentCurrentCredit;
//     std::string spentCreditStorage;
//     std::string newCurrentCredit1;
//     std::string newCreditStorage1;
//     std::string newCurrentCredit2;
//     std::string newCreditStorage2;

//     abi.abiOut(&out, spentCurrentCredit, spentCreditStorage, newCurrentCredit1, newCreditStorage1,
//         newCurrentCredit2, newCreditStorage2);
//     BOOST_TEST(spentCurrentCredit ==
//                "CixBbG9HNDhZZldMVldLRTQyckpTN1JZTmhzbk1MR0V6aFN5emJ6RVZCV0cwPRIYN0Y0Ny84dm5UeFNZamU"
//                "xR3dHZnRZZz09");
//     BOOST_TEST(spentCreditStorage ==
//                "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSEM3L1NKbVFxc0k"
//                "rclBxZEY5cGJNejU5TTYrVTl0NmI3aEVvSEJ6ZXgzbllXZ3N4ZndFYWtBN0JvMWRodzY1a01lNVN1ZTRIdz"
//                "VzTHRRNHBFQmhQaXJBPRIINWViMzZiODUaSAosQWxvRzQ4WWZXTFZXS0U0MnJKUzdSWU5oc25NTEdFemhTe"
//                "XpiekVWQldHMD0SGDdGNDcvOHZuVHhTWWplMUd3R2Z0WWc9PSJICixBbG9HNDhZZldMVldLRTQyckpTN1JZ"
//                "Tmhzbk1MR0V6aFN5emJ6RVZCV0cwPRIYN0Y0Ny84dm5UeFNZamUxR3dHZnRZZz09");
//     BOOST_TEST(newCurrentCredit1 ==
//                "CiwxUEM5ZHJyN1c2TnhTeTYrbnNsZGZDOVRQVE11K3JLYjUraUtCTm5ud2dFPRIYYVZnak9wR2JRVjJieEV"
//                "KRHcySWZTUT09");
//     BOOST_TEST(newCreditStorage1 ==
//                "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSEM3L1NKbVFxc0k"
//                "rclBxZEY5cGJNejU5TTYrVTl0NmI3aEVvSEJ6ZXgzbllXZ3N4ZndFYWtBN0JvMWRodzY1a01lNVN1ZTRIdz"
//                "VzTHRRNHBFQmhQaXJBPRIINWViMzZiODUaSAosMVBDOWRycjdXNk54U3k2K25zbGRmQzlUUFRNdStyS2I1K"
//                "2lLQk5ubndnRT0SGGFWZ2pPcEdiUVYyYnhFSkR3MklmU1E9PSJICixBbG9HNDhZZldMVldLRTQyckpTN1JZ"
//                "Tmhzbk1MR0V6aFN5emJ6RVZCV0cwPRIYN0Y0Ny84dm5UeFNZamUxR3dHZnRZZz09");
//     BOOST_TEST(newCurrentCredit2 ==
//                "Ciw1RVpFZDBQMjhwQzZTbGM1Z2l6Q29tSWN1TzVYQ0VWcTJzbnFvVzF2QUZVPRIYTTBjZGJwRFRSOWFtVlB"
//                "MOVFuSG9MQT09");
//     BOOST_TEST(newCreditStorage2 ==
//                "CogBCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhJYSEM3L1NKbVFxc0k"
//                "rclBxZEY5cGJNejU5TTYrVTl0NmI3aEVvSEJ6ZXgzbllXZ3N4ZndFYWtBN0JvMWRodzY1a01lNVN1ZTRIdz"
//                "VzTHRRNHBFQmhQaXJBPRIINWViMzZiODUaSAosNUVaRWQwUDI4cEM2U2xjNWdpekNvbUljdU81WENFVnEyc"
//                "25xb1cxdkFGVT0SGE0wY2RicERUUjlhbVZQTDlRbkhvTEE9PSJICixBbG9HNDhZZldMVldLRTQyckpTN1JZ"
//                "Tmhzbk1MR0V6aFN5emJ6RVZCV0cwPRIYN0Y0Ny84dm5UeFNZamUxR3dHZnRZZz09");

//     std::string errorSplitRequest = "123";
//     param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_VERIFY_SPLIT_CREDIT, errorSplitRequest);
//     BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
// }

// BOOST_AUTO_TEST_CASE(anonymousVotingVerifyBoundedVoteRequest)
// {
//     dev::eth::ContractABI abi;
//     std::string systemParameters =
//         "CixDRlU2Z1V0UjRxSUhpWHBzcTc2VTZOOWFhNm1TMjNPcmtEWGllMXovRXdvPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
//         "==";
//     std::string voteRequest =
//         "Cs4ECogBClhIRnBxZ091MEZ1dlEyVmd5Z1VrZk1yZHJVNTkxYnAreUVYeThlVkhzZjhsK2ZLV2dSclVCWTBJRzRaND"
//         "FpVkhIcTJJTStoNnRsMGxSNndTU2pHMm0rTGc9EixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FN"
//         "TDU5MWVDMhJcCixGc0JQY0FOMkZJelgwNkROUHN6akNZU2xQc2Z4a2xaNUswT0lhSjBnR0NzPRIsWURGZThjVnNscF"
//         "h4aXlzZHpxdUxUY1VGY3Z5NCtQRUF2a25POW5BY1VsYz0aLgosWmdoZ3k4cmFzZTBjbUtEYVhBVWdUTEZhUlB0dlpv"
//         "dEhDNEhCdHlEa3h6bz0iZgoGS2l0dGVuElwKLGNsY2NCcm5hZjVGV05MNkRKOHdVNmpPTkhLOFY2VDB2MVlKSm95dT"
//         "RJa0E9EixFaXNGdGJzT2g2RC9ISE9JcFpBb2RvQUV4VzcrWWQ3alRNbFZVcGVRN3k0PSJkCgREb2dlElwKLDRuYlYv"
//         "TGpFcUpDVG1zZ0lEWEFUY0M3RUZEdGx1M2k2cllYK3Zyd3NibVU9Eix6RmticWV4bnpFdkEveFJKZmMrWlhzUWlQaX"
//         "R0c0dXN0g0Skk0N3dhamdvPSJlCgVCdW5ueRJcCiw4aUpmZHJYSXJFZEM0ODBaNHUya0VzeGhvUGFzTkY1MG02UEEv"
//         "c1liQ3pBPRIsc05Da0F1YWVkUEZVTXJQS1N6ano1OXg4YTQreDB0SEJkRkV4VDJwMTBCOD0SmAEKBktpdHRlbhKNAQ"
//         "qKAQosQ3NGUGM2MU5MemtwcEZHWS9IKzB5dzBhNHBseGJnMGlvelVsSHlGcytBVT0SLFJwaGVDRWJSckwzaFRFK1Vq"
//         "c2VJZ25Ld28rS3UxaEE2ZUNiaEJtVVRWZ2M9Giw0Z28zQ1lnb1F4TkMyQlh0Y0c3SENLci8rSk9RR3hMMElLendZWF"
//         "A1RmdjPRKWAQoERG9nZRKNAQqKAQosUFNWZEQ1V0xUdVc4NWw4TWwvRGtrY2RXYlhSbmNoOTZmKzZtUGV0dVhRTT0S"
//         "LEF6TklORW4wWDNXbmR6SkM4ZXZzaGRkSUM2MlpYeDhlVUVobFlyUGZUQXM9GixrL01qUk9lVklSa2JtU3laT2lyNG"
//         "dNbWdoVEY2clJKa0l4OC93U3lncmcwPRKXAQoFQnVubnkSjQEKigEKLDUxQ1QydEd5bGh4MHV6TlpueVlPNDRlZGVN"
//         "NkxBZmI1K05yKyt2MU5od009Eiw2d3htenY3bjNYeTloR1pmU3MzRFVoY251ckcySmc1b2xZK0RzNDNnaWdZPRosU1"
//         "hUcTZBS0NWSUZ1K3hQbVcrZnM3Ui9PWEFVaGRwbnpVVGk2NERWbEV3bz0agAdyTmc2SkpKVStjZjJHZGU3N0xzc2gv"
//         "cmZwUlZXcEdCd1pkS1IrRkdhQldvR1pmeGJpeXdWTmRXQ0dRUzRhclZZUHlVVEZWbkxYdVU2SGZqeVU3ZWZmZG9HMF"
//         "pFZDNPOGxFaE9vSzNjUGVTcjNJV3FleUFuV3dFd0dEd3d4ME9GSEdNKzlybXJ4WFZwTEFWM1N2V0szOFFxVnlReDEz"
//         "SlBpeFNBWWY0SWp4UkFUSitEaGlrZmw4dTZxVGRYYjlidUNkZ2t2STJsNzNrWURxbmlhS202REFPblpEbk5FL29kZ2"
//         "h0UDdPY280THNxSFlDVGk2a1JOVzZqbFpCK2JyR1VDQWxtZlBVajg0cThEU20vMFRCSWVWblR4QlV1LzNlYXFMTHls"
//         "QVVNald3KzJkbm8vL3hHWmgrZEtHd2JMU280cFhSVW1maGlhZm1Nekx5ZEY2THpvTXRaRmEyQ25tdnVQQWZ4TFNBRE"
//         "RtSzQxd05qOXg1QmdDeEJkdmNVQ0Z4MVBlaW05UmhwZXAwL0laaXg0enRlbVQvV1REUG1WSWxEaFhpeko2V3F4NVhx"
//         "WXpNYXRUMk55WDZTYXVCcEhuU0p6OTVETnBMOE9EU29JZkxSR08xSFFRZ2dCY0ovUGRkWFlzY29Fbjc0YXFiU242dD"
//         "V3NTJFMU92eWM4eFgzSDk1UnBJMzlxSGp0TXhvM2FxcmFTZWFKSU9tVDdKY0cvUWZteEg5T3JmbGxybEJRN1pIQkMx"
//         "c0Z1RW1kQnRlbDV2ZGoyRm1ldy9GdE5Ka0FBdy9yUHZST05mSk5wOFFKb0N3OWxBTnVrZFNIcnNaMXQxcWNxbGhIUW"
//         "Yzb1gyVXNOSDRjMERnT1JOLzJOMmsvTFhmWWQxSkdzY3Qvc2ZtS0tRUmF3WThaUVFJSnlXTThvZFBydEwvSFRCaTZq"
//         "Nms1S0NlMmV3ZW1KYnErMXBhRmNuQXNpMmFZUzl3N2Z0QzNJMXFTMERNb1Zvd3VpaFd0NkQraDR3cWJXRnAwMFZYRX"
//         "VJMVpRR1NCQnpCWjFpTENBUEpXWTFUZGp5QjRMV0tMdTBPTnp6aTlmYjcrYjAreUVWMHdJRnJZMjlOelZybW1KalFJ"
//         "cmNDZFVnWDFRVkdTSjNIbDJUaGdBUnVxYUVOem0vMUVpZEJwQVJpOW4wWVJUS1VFdjRUK2sxTVAzSlY3bDZnTCKUAg"
//         "osV2NFT0RuMU8rSnZzRko1RDNjNmYwNTJOV0JobGxNUmFBQTZNVnMxalNBUT0SLHNBRHM5VWVJYnByZ3lrMWpnSXRo"
//         "SC9TckVPQmlXcFlsV0tWVzREYXQ0UVE9Gix3R2JnOUk3c0xXeWRDbnRiUlR5UmtPN2YwUVRlSldYTUsycmtUR1orYk"
//         "FZPSIsVTJxZ1lYbFF5d2p6U2V4RTBod00yakM0aGxSVHByTFE0QXkvVm9DaTlnbz0qLFlxYWRTZmJ6N2F2dUErTEU4"
//         "NTBsb2E0UlFXNm1RSXZrOW43dDVXQk10dzA9MixLd1lHUHV1eWtWb25sY0NHdVp1Wjc0UmF3QTR6M2o1TEVUMGVGTE"
//         "VXVndJPQ==";
//     bytes param =
//         abi.abiIn(API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, systemParameters, voteRequest);
//     // anonymousVotingVerifyBoundedVoteRequest(string systemParameters, string voteRequest)
//     bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

//     std::string blankBallot;
//     std::string voteStoragePart;
//     abi.abiOut(&out, blankBallot, voteStoragePart);
//     BOOST_TEST(blankBallot ==
//                "CixGc0JQY0FOMkZJelgwNkROUHN6akNZU2xQc2Z4a2xaNUswT0lhSjBnR0NzPRIsWURGZThjVnNscFh4aXl"
//                "zZHpxdUxUY1VGY3Z5NCtQRUF2a25POW5BY1VsYz0=");
//     BOOST_TEST(
//         voteStoragePart ==
//         "CogBClhIRnBxZ091MEZ1dlEyVmd5Z1VrZk1yZHJVNTkxYnAreUVYeThlVkhzZjhsK2ZLV2dSclVCWTBJRzRaNDFpVk"
//         "hIcTJJTStoNnRsMGxSNndTU2pHMm0rTGc9EixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5"
//         "MWVDMhJcCixGc0JQY0FOMkZJelgwNkROUHN6akNZU2xQc2Z4a2xaNUswT0lhSjBnR0NzPRIsWURGZThjVnNscFh4aX"
//         "lzZHpxdUxUY1VGY3Z5NCtQRUF2a25POW5BY1VsYz0aLgosWmdoZ3k4cmFzZTBjbUtEYVhBVWdUTEZhUlB0dlpvdEhD"
//         "NEhCdHlEa3h6bz0iZgoGS2l0dGVuElwKLGNsY2NCcm5hZjVGV05MNkRKOHdVNmpPTkhLOFY2VDB2MVlKSm95dTRJa0"
//         "E9EixFaXNGdGJzT2g2RC9ISE9JcFpBb2RvQUV4VzcrWWQ3alRNbFZVcGVRN3k0PSJkCgREb2dlElwKLDRuYlYvTGpF"
//         "cUpDVG1zZ0lEWEFUY0M3RUZEdGx1M2k2cllYK3Zyd3NibVU9Eix6RmticWV4bnpFdkEveFJKZmMrWlhzUWlQaXR0c0"
//         "dXN0g0Skk0N3dhamdvPSJlCgVCdW5ueRJcCiw4aUpmZHJYSXJFZEM0ODBaNHUya0VzeGhvUGFzTkY1MG02UEEvc1li"
//         "Q3pBPRIsc05Da0F1YWVkUEZVTXJQS1N6ano1OXg4YTQreDB0SEJkRkV4VDJwMTBCOD0=");

//     std::string errorSystemParameters = "123";
//     std::string errorVoteRequest = "123";
//     param = abi.abiIn(
//         API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, errorSystemParameters, errorVoteRequest);
//     BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
// }

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyUnboundedVoteRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "Ciw2aXZlUjY5UjUwU08wQ2tiejhlMjhEY0Z5Z1N3RmRHdFJnVVdoUEZ4blJFPRIFQWxpY2USA0JvYhIHY2hhcmxpZQ"
        "==";
    std::string voteRequest =
        "CvwECogBClhIT2g3K3NLbUliZTcvYTJXNE8xNlF1a1E5a2hSc09zenNBdHB2bnpmVUpaVElkRXhVUTZQUEhOU3pnRU"
        "ZPNkV4N0FMYWJTekI2Y2FKSnpxZC90RFUxVHc9EixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FN"
        "TDU5MWVDMhJcCixMdEJXZXo3Um5qVWdkREJFcGNLVzlrTG1OLzZzckhoaEJOWVArRjVKMEUwPRIsZ0liL1Q0U2Y4d2"
        "MzTTZkRHpvSU13S0xtOTlBS2NtVlYvUndRRi9VZTh6bz0iZQoFQWxpY2USXAosWnVRYmFRUFNtb3BGcnhxSXpUTzBG"
        "ek9Mb2FxTy9va1hwZU94Z0h3YnUxYz0SLHhzS0pvY0FJbkpOMCtteWNsNDUwR1J6Z2pYaWtYYVdFMnMxaEFGU3F6Rl"
        "U9ImMKA0JvYhJcCixJSXdNTWNsN0lRWitZdlVVTUJWL1pYc0VIRGFqT0lyUlZrWjIrczY4WFhrPRIsRUpGTzBET0s5"
        "TVRvc0FKUXhBb21KenlXVGR5MEh3clFmaU9EaVhvQUNoYz0iZwoHY2hhcmxpZRJcCixWRGJvSzc0NW9xTmRPSEx1bS"
        "tRWDVEZzRMV3BaNEdGek9UL1ZuLy9HTDNnPRIsTWlkZW8wYVBVVXJsWXBQeXRKajFUYTFMVmJSNFlOajdiY3NoRjYr"
        "dkduTT0qXAosUkNBbjhtOXBXcjgzZENEd1UwcWhhemg5QTRmRFI3ZTJCQ3NmYnFaWTVSaz0SLG5GOEprSngzWmJPQk"
        "9FWU44bG9tRnQyWU9MSmxVYVBWUlUrNW50SkVRRFk9EooECgVBbGljZRKABAqKAQosZ1dDSm05cWJ0Smo4bWNsbEF6"
        "NVZ0K1pWNVYzTG5zN2doS1JPU2hSd2F3az0SLEFlWTlsWDhBU05haGF0VnMzNTFib2pZMnU3VUFUelpHckRxSlhZRn"
        "FSQTA9GixwOUExamNGMEVVNkZyZ1NiWkU4U2RTWC9aRmd2enB2dDdGMGZQTCtQeUFFPRLwAgosZEJZUWpRMVVyZU9D"
        "VFVrY1FTcXlDQ1BwNy9pb2M2d1ZhZXJVYlZTV0VBQT0SLHZvVkJQYk41NXZWa2htQUE1UUM5eUIvdDZnUkNLRVJxZU"
        "I1TUZZcW45UUU9GixZYmNBL241eTVzaGNnWFZkVTM0K3Y2S0ZzaXNmalJKNzFrdHlJQUlnb0FNPSIsUGRvL1RwWWZJ"
        "R2NCRGxINzZBYS96NVJua3Ftb29LL0REbmlydHFBeWRnRT0qLDYxSUxJV3RXQzJVMEY0dnhZYjV0Q3Ewa1lpbnJ4V1"
        "kzNDVBRjdWYXRyQVE9MiwyRTdabmFobkNvRkU5bnErbWNKMnB2Nk1NUGdNeGhBT3dQc2hOSkZ5VFF3PTosQnI0dm8y"
        "Qk96ejVHd0hRSU0wOUR0eWpyamgvVDRGSlh4cURsWmh1aXlRUT1CLHlCS2gvNHprRE45Q1ZKaFhKZkc5UVhvVXdLak"
        "hLNGNOZi9Xamorai9Kd2c9EogECgNCb2ISgAQKigEKLE01VDRtZTAzK3lRTmNidHNOYVlqamZBRVlEaCtjVkdjckVX"
        "bDh1clhsQWs9Eiw5bmliaXBTeWdCRCsvS1dmK3lFNzBySUZ1R1AyYmo4S01yakZPRENYTXdjPRosY0pyRGpqTGRwOF"
        "kyVDZOUmQ1U0cyWEZ5RFRDYjlKRDZNTkd1R0VIbER3QT0S8AIKLG1HTW5haEE0QmIrU2pvMm1VN2VGVmQ2aUtmY3Mv"
        "bXl3aENVL2lkemlOQXM9EixzVDYzMThMdnNPTnlJQ3owczNmT0E4blo4QkZnN1VkUjN4eDVrN2t5Y3djPRosTUZNVG"
        "9ZdG8xZlI0ZklMWGh2OTNtRjdhSUlkRlUwQzVLUEt4a1R5b3lnaz0iLE5PVklXVW9xYnluQVFsUzh5NFZ5MHRuMlk4"
        "YXVzMktMMS80OWREQTh2QU09KixMMFhrbTZpK3B3SDVHZHIxQUtMRjQwNk9mTFM5alE4d0Z1Yk1xWkwwVWdZPTIsdX"
        "VhTzM3cGZmc1RHS3VmQ3BEdmpoeEJIUWRoVktCekVWeXhUbjRueWNncz06LFJGWm9tVU1iY1I4WlRhTUdiMkRRWmJ3"
        "dEFUdUt4LzBZaTlyT3JGVnNvZ2M9QixCdkJBY21GVUxNTVY3ckdCMG0xQzhPTHpIQzYwNTdUckcvNHd5aFl2d1FvPR"
        "KMBAoHY2hhcmxpZRKABAqKAQosZXFOdlZhT3NhMURxZmlFcHdOdkp6RXdVSjl5Uy9Zb09EMytVRUJibXRnWT0SLEd2"
        "SXdkdHFOWWU1Z2JUMEZOOStIREk3VGhQRSt3akFVbnRPNW1rUXg3Z0E9GixsTXFKcVErK2JYWFNpN2JoU2VCZTVKUU"
        "huWW05UmNIcFBIRklLOEJFVUFjPRLwAgosbDJFQmVxNkszMnhzK3hSVFlmVThMRUZxU2pIWTQreHNqZm9oUU1WZkZ3"
        "UT0SLGk4SjB5YVR6QjQ0TC9oVy9aaVF0Q1g3bHl6NERLVEtsbEtyYWMrV1Fad0U9GixZTUdDcDQvQVN6eUdwaXorNE"
        "1Yd0dGMzBZcWFvdHNDZWIxOUZIcnA5aFFrPSIsc3g1czd1R2hRalN4alVETktmQWJSV2RYbEp3RnN0NklsUEgrMktG"
        "UmVRND0qLExlaUcrY3ROUU9qcTlGejRnTGtUWHdXeUUzVlRtb3hSYWNxNlVDR2M5UWM9MixkcWgyL3U3Z1l6WFR2OV"
        "Z3TktqMFRmb3AwSjVjVHE3M0hOQjZqVEZrUXdnPTosWTlPN3djaHhBdDhnczJVTUVwczJLL3hXRkVZZmJYS0V2UUY2"
        "cWFBTTNBOD1CLGUwa2JpTk9EMFpadVBVMG5IeEJOUHpiMGJ3WHU0RzEwSS9yWTlnRHBId1E9";
    bytes param = abi.abiIn(
        API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST, systemParameters, voteRequest);
    //  anonymousVotingVerifyUnboundedVoteRequest(string systemParameters, string voteRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string blankBallot;
    std::string voteStoragePart;
    abi.abiOut(&out, blankBallot, voteStoragePart);
    BOOST_TEST(blankBallot ==
               "CixMdEJXZXo3Um5qVWdkREJFcGNLVzlrTG1OLzZzckhoaEJOWVArRjVKMEUwPRIsZ0liL1Q0U2Y4d2MzTTZ"
               "kRHpvSU13S0xtOTlBS2NtVlYvUndRRi9VZTh6bz0=");
    BOOST_TEST(
        voteStoragePart ==
        "CogBClhIT2g3K3NLbUliZTcvYTJXNE8xNlF1a1E5a2hSc09zenNBdHB2bnpmVUpaVElkRXhVUTZQUEhOU3pnRUZPNk"
        "V4N0FMYWJTekI2Y2FKSnpxZC90RFUxVHc9EixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5"
        "MWVDMhJcCixMdEJXZXo3Um5qVWdkREJFcGNLVzlrTG1OLzZzckhoaEJOWVArRjVKMEUwPRIsZ0liL1Q0U2Y4d2MzTT"
        "ZkRHpvSU13S0xtOTlBS2NtVlYvUndRRi9VZTh6bz0iZQoFQWxpY2USXAosWnVRYmFRUFNtb3BGcnhxSXpUTzBGek9M"
        "b2FxTy9va1hwZU94Z0h3YnUxYz0SLHhzS0pvY0FJbkpOMCtteWNsNDUwR1J6Z2pYaWtYYVdFMnMxaEFGU3F6RlU9Im"
        "MKA0JvYhJcCixJSXdNTWNsN0lRWitZdlVVTUJWL1pYc0VIRGFqT0lyUlZrWjIrczY4WFhrPRIsRUpGTzBET0s5TVRv"
        "c0FKUXhBb21KenlXVGR5MEh3clFmaU9EaVhvQUNoYz0iZwoHY2hhcmxpZRJcCixWRGJvSzc0NW9xTmRPSEx1bStRWD"
        "VEZzRMV3BaNEdGek9UL1ZuLy9HTDNnPRIsTWlkZW8wYVBVVXJsWXBQeXRKajFUYTFMVmJSNFlOajdiY3NoRjYrdkdu"
        "TT0qXAosUkNBbjhtOXBXcjgzZENEd1UwcWhhemg5QTRmRFI3ZTJCQ3NmYnFaWTVSaz0SLG5GOEprSngzWmJPQk9FWU"
        "44bG9tRnQyWU9MSmxVYVBWUlUrNW50SkVRRFk9");

    std::string errorSystemParameters = "123";
    std::string errorVoteRequest = "123";
    param = abi.abiIn(
        API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, errorSystemParameters, errorVoteRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingAggregateVoteSumResponse)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "Ciw2Z0FZZWphOVhVNWxlbkpJNFZ1bVdINEI4VkY2Z0hjVnliWVBpNXp4NFdrPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteStoragePart =
        "CrQBClhTU1ZUU01rQTlBTGkvMW9qNy9PekdCMnQxR2ZJUndtU24vb2MvNnRPSHlNVG1rSGtSMzY4cWZHall0dEN2aH"
        "B5YVE0cTlvOWNEK3FTTUNET3pldjZCQT09ElhCSEcvTUdVUFlXVjFVTUltR0sxNTFHTEYwWkZ4aFRSNGtCMk16VW5k"
        "dlF1RGFNVVNWNjhUYTBiRmRINGtYcTZlSnZuNmw1QUVJY3BqVHhoVXpiV1ZYRzg9ElwKLGluT3Z4SHBNQnBLMUpUdV"
        "NLcjFYT3RzQ3ZQaWhRR0FDRG9lV0wrVGlSbk09EixmcnBPdk4vWUNUZUZrRmJUMVdCNzVqcnBlbDNmTlgzUThSSlNk"
        "RCtHTzIwPRouCix2QTZFVzJwRERHU1dMN0hmOVRxbVRkaElRcSs2OFh6RjhLcHd1UnA2eFFrPSJmCgZLaXR0ZW4SXA"
        "osd3FaaFZCWC9IRHV1cEl2R1VCck5WUTRlcDc3ZFFhOXNjc2l2amVEOUFrMD0SLEVubWNhSi9FdzlRREI3R1l4b2tz"
        "cFEvNjI2NW8vT3pSdGs3Z3ZDdGRuRVk9ImQKBERvZ2USXAosOU5xVk5Ob24yY2RsVFVtNHJvOWpWQkZkbVNHZG9MM0"
        "1nQmdNRHFiVHNtMD0SLHRyK1B3Z0tNbUF2a0dUNm5vWFZsN2R0RXUxNEdJMisvWC91TjUvdWI3bXc9ImUKBUJ1bm55"
        "ElwKLHhBdGJyR0RRdEhsOFllUkp1R1kyN2w5L0Zndk5LMVE4MzR0aE5aTkYyRG89EixzS2ZNbEFoVmI1Q00rMzBLVG"
        "ZqRk5IS3R1NTRCQ1U4TVV2MWxMTHpzQVZJPQ==";
    std::string voteStorage =
        "ElwKLHVuYkZKb1liNVc1OG1DUUJMVmI3SzBoam9pY2JKM3lrT2RrUnU5cUNFVEE9EixDTi9MbFB6VTBkcFplSWROTW"
        "pvbWoxTVBpTHQ0bkZNNS82NmNyRHlwSXc4PSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosRW1UNEZKZHZSVnU1dEtIVU9JajhtU0YrSGQvTGF6bzNsNVhZVUxzU2VDbz0SLGp1cGRETGJXUW"
        "E0RSt0dy91RDRLS0x5WkIyeVNFMk40U1cxeHNvR09BQW89ImQKBERvZ2USXAosMkpzTUZSUW9Xd1RQVjRjMDgrajlU"
        "UmpUL3JQMGhPYlJoNHFNSGw3YXNTdz0SLHNIOFdYSGRnQnM2b2l0R2FBdFpQNWQza25HUWthRXpzajBHSFU2Wm5MVG"
        "89ImUKBUJ1bm55ElwKLDdLaVFrMU9xajlBRVhURXltR0ppalplRkR4Mm5JeGpTcDRUdGRWbEZxa2c9EixRTkYwUEdK"
        "OGtOdHlZL0JlZ0FrbVcreldlSTByYTlGdXAwaWlhTms2S0UwPSJmCgZLaXR0ZW4SXAosNG5sM0FVbDloWmxEZ3Z3bj"
        "hIcytHVzNXZys0R1dFanFFUXhoTFMrZlBFUT0SLFloRkhlRFFlaTNOdUtRNnVtclVBa3Y4SExNV0hXeVRVRWs2SVBj"
        "dDFQelU9ImQKBERvZ2USXAosU01pTTFUUzVuS2ZuWVM2czRLU010d1ZITUhjNXpIU3htcjlIMWlDUVgxQT0SLHhqZG"
        "pMa0pzRDZnVUw2aFpTNDhxR1VCTldxRTIwQ2RnSzgzd3lIemNoZ0E9ImUKBUJ1bm55ElwKLHZxc1ZWdjkxMjlYZytO"
        "OCtxc2dJUnh6cng4WUJDU2dYSW9rREFDaXFYamM9EixESkxXbVhRcGZGNktaRmJEa3JXVVY5a0FaR1hkZ1VXWitGS2"
        "RndE9zaHdrPQ==";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE, systemParameters,
        voteStoragePart, voteStorage);
    // anonymousVotingAggregateVoteSumResponse(string systemParameters, string voteStoragePart,
    // string voteStorage)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string voteStorageSum;

    abi.abiOut(&out, voteStorageSum);
    BOOST_TEST(voteStorageSum ==
               "ElwKLC9oMlNUNlNEaU5qSlBndWsyN0ZVQkJaRE5uQWtkTmU0SEF2ZStRci8yMTg9EixGc2RMUjVIbkxXMTV"
               "2WjdNVmswbWhmQmFWNStEaVJQUlVUNUQ5WVdnMEV3PSJmCgZLaXR0ZW4SXAosOGswaFdSTGU4bkppQTVMV2"
               "VXeFprcUVQcUdnWTNkZFZtcnpBcFdtWWdtZz0SLDZnL1ZJSHk2OXRxUnd0bWdpRFV1cmg1Ym9oVlczejQ0Y"
               "Wpmam1HS2dwbE09ImQKBERvZ2USXAosQkx1QnQvR2JRRERydlJENlkwMjNnc3JFMWFEdlhvUHZ0R1FVTTRD"
               "ME5UOD0SLHNtQ0hPMjlLMXRqY1d0RmRxTm04Z1prbTUwUjh2UlV5ZnNvWDFENy9laW89ImUKBUJ1bm55Elw"
               "KLFRxejNaNVJGUStzcStxdG1XVXhoNzhlZ0FPTG5tYytBMFV0Y09hTUx2U0E9EixPcFBPMzlZc0Z6dTBwbT"
               "J1bHQzN2ZZK0htREFpUFZ6ZFcwRGxxVk1oNGdvPQ==");
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyCountRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixvQVYyNTd4c1FaSmhwdGJ2cWFXNEppNmtqTUI2dnJTRXYreVd2TUgzVVRzPRIFQWxpY2USA0JvYhIHY2hhcmxpZQ"
        "==";
    std::string voteStorage =
        "ElwKLFlOZkFuSjcwOG95UWE5TmIyaFpWRnFNOVdFTWlZTzVXZHd3SFlBRUt4QTA9EixNa2IyUlNGd2lveGEyL2l3UX"
        "JuZXBNZGhBbm1lMXpEcWlwYnJtakFsQXpNPSJlCgVBbGljZRJcCiw1RzZNVVdSaGY2Znk5NGxPck9wTkhuQkFrOWV0"
        "bkdseGI2ZndpaHRET3g4PRIsY21uVTd1clV4YnR6UGJsMWYwU3NFTEx3dTV0Yi9IS1p1VC9LcGh5dzJGND0iYwoDQm"
        "9iElwKLDJMbzBFWU1GcGpOb09zS3IwSGFyaWY3L2FEUzBOaVZaVTNBR2hZeGIvbk09Eix2THlVb1lSbjZsMVZUVFRG"
        "V3p1MWlRRFBrT2ovZ1pTL1JmQXFmakVpcG5VPSJnCgdjaGFybGllElwKLHVuTWlCVTFmdTRyUWJyS0h3d25ydnpOUj"
        "JvdE1jd0lNa2hzMFcvNWNVekU9EixPRitndnFrKzBNc2RBS3FqTG5IYTM3b3A0SzJPempvY1JoSGVmR2pJc1g4PQ="
        "=";
    std::string hPointShare = "yMa4n7p3lWXGsjNsjG/SkZq1MqhPa+4SfN8PPQstc0E=";
    std::string decryptedRequest =
        "CuEECpMBCgUxMDA4NhIsdUQ1dnpPdUhTQ2hPcW9GVWw4TDBvTmJJNjhZQ3NZN1IrRUV1QnByZ0RFRT0aXAosZDdmdm"
        "xZMHl1YWcxU2lUZk5xVFhRMFB1bUx2eTZWYUd3WjMzbG5kVUpRZz0SLDNDeWZlVFN4V1ErVExEanNzL2RaZnpuQ1ZH"
        "SDNPWU9YSmFnQVJ0YmJOUXc9EpYBCgVBbGljZRKMARIsYXJ5akZwYXdPSHdCQjlRNGh4N0lRZ2ZHMG1ZcnNzeUI4aW"
        "s2aFFuRlVHZz0aXAosbkl4T2tWb2pKbTZsY1hPbW1xZXEwb0MzVnA5LzBOT21LaDhHOWx6ZEJRcz0SLE1vbjBpMWE2"
        "STQvTlpVdW4zT3lKMDF2TzlQbW1wSVRMdEF0NTVLaFNkZ3M9EpQBCgNCb2ISjAESLFRvTitBVXB4OVEydTZ6T2xXY0"
        "xZUmdQMFRkS0F0c3kzNk5HSjFJZDdnZ0U9GlwKLEZnNEs3b25vQlc5Zk9tNXU3Ly9hdTErZllvWG1jdlN3VVF4eC9z"
        "Nk12UTA9EixKZ1NOUElDZ2NyUWNaWU5aNCs4eEZTMXJtbmw4OG91SVc3RnlTZllyMGdjPRKYAQoHY2hhcmxpZRKMAR"
        "IsRmx3RURueWZrKzZxVExMM3ZtS2h2Z0hTRGhZMGZqYWdtQ1Q3am0vY2JUND0aXAosT3hRai92Mm1aQlBZUHdZT1pQ"
        "T3dXWlE0UVBKOW1RN0xTcXdBQUMxemJRZz0SLEZrTzJUSm83RW5nRXFrVWRwZGkyNmh1ODlJWkI0UStleVpQZFZtaG"
        "lwZzA9";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST, systemParameters,
        voteStorage, hPointShare, decryptedRequest);
    // anonymousVotingVerifyCountRequest(string systemParameters, string voteStorage, string
    // hPointShare, string decryptedRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string counterId;
    std::string decryptedResultPartStoragePart;
    abi.abiOut(&out, counterId, decryptedResultPartStoragePart);
    BOOST_TEST(counterId == "10086");
    BOOST_TEST(decryptedResultPartStoragePart ==
               "CpMBCgUxMDA4NhIsdUQ1dnpPdUhTQ2hPcW9GVWw4TDBvTmJJNjhZQ3NZN1IrRUV1QnByZ0RFRT0aXAosZDd"
               "mdmxZMHl1YWcxU2lUZk5xVFhRMFB1bUx2eTZWYUd3WjMzbG5kVUpRZz0SLDNDeWZlVFN4V1ErVExEanNzL2"
               "RaZnpuQ1ZHSDNPWU9YSmFnQVJ0YmJOUXc9EpYBCgVBbGljZRKMARIsYXJ5akZwYXdPSHdCQjlRNGh4N0lRZ"
               "2ZHMG1ZcnNzeUI4aWs2aFFuRlVHZz0aXAosbkl4T2tWb2pKbTZsY1hPbW1xZXEwb0MzVnA5LzBOT21LaDhH"
               "OWx6ZEJRcz0SLE1vbjBpMWE2STQvTlpVdW4zT3lKMDF2TzlQbW1wSVRMdEF0NTVLaFNkZ3M9EpQBCgNCb2I"
               "SjAESLFRvTitBVXB4OVEydTZ6T2xXY0xZUmdQMFRkS0F0c3kzNk5HSjFJZDdnZ0U9GlwKLEZnNEs3b25vQl"
               "c5Zk9tNXU3Ly9hdTErZllvWG1jdlN3VVF4eC9zNk12UTA9EixKZ1NOUElDZ2NyUWNaWU5aNCs4eEZTMXJtb"
               "mw4OG91SVc3RnlTZllyMGdjPRKYAQoHY2hhcmxpZRKMARIsRmx3RURueWZrKzZxVExMM3ZtS2h2Z0hTRGhZ"
               "MGZqYWdtQ1Q3am0vY2JUND0aXAosT3hRai92Mm1aQlBZUHdZT1pQT3dXWlE0UVBKOW1RN0xTcXdBQUMxemJ"
               "RZz0SLEZrTzJUSm83RW5nRXFrVWRwZGkyNmh1ODlJWkI0UStleVpQZFZtaGlwZzA9");
    std::string errorSystemParameters = "123";
    std::string errorVoteStorage = "123";
    std::string errorHPointShare = "123";
    std::string errorDecryptedRequest = "123";
    param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST, errorSystemParameters,
        errorVoteStorage, errorHPointShare, errorDecryptedRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingAggregateDecryptedPartSum)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixhbDQwd1V1d0lYY3lKY2dWdGJjRWhDK0IxMjl1TnNOa3duNXBPbTNDQVg0PRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string decryptedRequest =
        "CpMBCgUxMDAxMBIsUkVaSFRsWmw5YVRaS0h0SWdpd1JwUGFGaFVuNXdnMjBMT2RRbXBrSEsxcz0aXAosbVpFY3hQVH"
        "BQZVNlcUJGOEZEK3F0Q2I1TXZoTk84a3FnWURTWGxqWHF3UT0SLDJvUG83ZW5iZzErR3Z5MklrcHNFNElxenVyaGRU"
        "NWtSNnBuMWlGdDNNZ1U9EpcBCgZLaXR0ZW4SjAESLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUE9GlwKLG1abDNGYUJYcEU0c25KbE5kNWZOejRnMS9CdnA1TmtXVkVUSzFHT0dad1E9EixObko1MEJ0citq"
        "QlpYTU91NnVwVzFTMUdYUjJQcUxzT09GWlpPaVlTVXdBPRKVAQoERG9nZRKMARIsQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosUDFVWklvSWZCWlRMalNCQ3FscDJYRzlTYVNSeXhXMkx1UkY5dTR4"
        "eHFBbz0SLE9pZUFiS2t4VGlNTmZOd0RhOElLTnJla24xWjhtOWREbnRCb0JVc3Iyd2M9EpYBCgVCdW5ueRKMARIsQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosenNISEtkZ3Q1d2hBbUVBenVvRWsz"
        "THo2aGpHQUtRS3JtR1ZqMnpOZ3d3ST0SLDhmVG1FTWN1dkpRb0UxbTNaVVlQWk4vcHUxcWlraGkzYktvaXFMMXFaZ0"
        "09EpcBCgZLaXR0ZW4SjAESLGREUjUvU1F6ck96WTF6ekwvZDhYRWlNUThSamtLdXdBNzN6TzdBVTBFejQ9GlwKLDZJ"
        "SFhhZ3RLSkdvclBSeFpEN29VUmhybFhpWG9CQytORnNHUFFUajBVZzA9EixHczA5YVU4K0pURGxtZm16RkF2dlE4cU"
        "tMazB1YndyYlNZd1FxTER1bEFjPRKVAQoERG9nZRKMARIsTEhIWXRwVmRxa3hnN1NQU2RvMU9MUjdoQUFXTTl5NjVn"
        "ZHdwMVB6LzUzcz0aXAosLzhTeERQbmVUSC9IYjJjVG84dTdpOEFUaU9ldDVRSFU0OHpMRjE3VGV3VT0SLEMyNlhhVV"
        "NWcm1sbWxucXdST2JTSURRbmU1bHRWR3JFOXNtWHNWaWVUQUk9EpYBCgVCdW5ueRKMARIsYkUvd0NZa2FuSjFESnVj"
        "clRaTHkzUWxVa2N3LzNucU4wNXBNcnEwMVRHZz0aXAosbmU2ZDlRaWZnbzZWTlkrVHlGaXlzeWJTcDUvKzBuQUZrUT"
        "VKRDZqaFRRdz0SLDdEL2c4dG4vdGs1VGpUMnFSQ0JVQ0JBS052SHhiVVI3ZFZKTlZ2cjBTUTQ9EpcBCgZLaXR0ZW4S"
        "jAESLEdwU2xUbDdWYmkreWFiay9rbUZsazZuUnVJVWE3RXFtVVYzWER0Z2QwMnc9GlwKLHVYbzgzb25yU3h2Szlzdk"
        "U3d3Ziak10UmJ6NldSTDFiVHlaV004ank3dzQ9EixEd3NxaDArUTRhaUErK3doUlQxd2RCdDZqcGNIS0JTN2FlWHBh"
        "QUY4bHdvPRKVAQoERG9nZRKMARIsZHJPYUd0LzI4Y21Xc2RQdG5BQi9iR2RrN2NrZXZFSEdjZGt0QzFsa3hYMD0aXA"
        "osZmFYa200THZ2dFZ3eDU2cmpMRU8xMXJ6VjE2eExFZ01PL3NvR1Z5aEtBZz0SLDF5SFNIUTEreXhlVmVaRUYzQlh0"
        "aTFHTUVkcy8yRk5sZm5DVEZSQnlrd0E9EpYBCgVCdW5ueRKMARIsd0NDRGlHcmZycEx4OWNmYXdJNWFiUmRFS3VBaH"
        "kxemRpUWNwYkNpcGlDcz0aXAosOUxvWm0yMkZ1UjFiQmdseWVRZEthTjlqeTB5ZTJVYXFJWGVjNHZ0UjFRQT0SLER3"
        "RHRwSXhRWC9lVjU2NU1qWGpGT0NxeFdLMmhuWXdiTzFPdzZtOXpoZ0k9EpcBCgZLaXR0ZW4SjAESLDVtUFlyWWgzWl"
        "JGVGRFOWcyMTZnWjRYZGFua3R1aUl6d24wTmg4RUE5UjQ9GlwKLDcwV3dkVFJYbW5vWUxQQ2ZyTzhqU1I0bmlKaUd0"
        "ZjU1c000Tk5hcDJzZ3c9EixDOFR0UVBxa2tXdFFZdXJBTUNJNUxpbmtvaXZDMXhMUkRvMEJyMEptakFnPRKVAQoERG"
        "9nZRKMARIsMGlWVU1YSFozQ3JpbnNHaTVTYkdEcE9IcWI1dzcwZTZMZTlXMDMrM2ZtOD0aXAosY01NYUxoWmFuUGFz"
        "b3FFMm5RZVpBempYQmd0MnFYYTZpb1VFTTRlOHBnMD0SLEhiMUNqTWRNbDhvVHk0THVScEVxRW5kWEt6K2phcnkyY3"
        "pCV3JxNklDd1U9EpYBCgVCdW5ueRKMARIsOEFEOXRuTndSTzhGQXAxaXpIOStDMWdraG5kUHR2MHlXRmdlc25WVkFF"
        "ND0aXAosTnltV0hXcVZRbnJRMFM4WlZGc2UxZzREQ2dHYjhQdmZZY05HbENhQ1lBWT0SLFpSR3ZlbEZkci8yYThiN3"
        "RrNTBMUkJJUlpVUlhWaC8wU3MwMHlvejNYd009";
    std::string decryptedResultPartStorage =
        "CjcKB2RlZmF1bHQSLFZIMFlscVhVREkvU3lDMng5Q2VRb3JFOGxEOGNvS21NUE1kWnFMdVE3VjQ9EjgKBktpdHRlbh"
        "IuEixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRI2CgREb2dlEi4SLEFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjcKBUJ1bm55Ei4SLEFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjgKBktpdHRlbhIuEixCaC9pSmxZTWNvb2lLK2V0Zmp3SE5GYVJZdVhX"
        "YjFFdUpIWlNSTXNZdm5BPRI2CgREb2dlEi4SLGxOdDEzTkZIS0cxUi8yaUtiR3NaK2dBbmJmT0Ridk9qZmhoZHM4UW"
        "dIREk9EjcKBUJ1bm55Ei4SLDFHZTByNjJtUUtSenBPdVZ2d3d5bXpKV1Y2QzNvMms0dmg5N0VwOXhhVUk9";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM, systemParameters,
        decryptedRequest, decryptedResultPartStorage);
    // anonymousVotingAggregateDecryptedPartSum(string systemParameters, string decryptRequest,
    // string decryptedResultPartStorage)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string decryptedResultPartStorageSum;

    abi.abiOut(&out, decryptedResultPartStorageSum);
    BOOST_TEST(
        decryptedResultPartStorageSum ==
        "CjcKB2RlZmF1bHQSLERKcTJCUGx2N2duMUFPMU9nMWZrVTBGN2pmeTRWT1pyQkV0ZzhMR0VlRzg9EjgKBktpdHRlbh"
        "IuEixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRI2CgREb2dlEi4SLEFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjcKBUJ1bm55Ei4SLEFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjgKBktpdHRlbhIuEixCaC9pSmxZTWNvb2lLK2V0Zmp3SE5GYVJZdVhX"
        "YjFFdUpIWlNSTXNZdm5BPRI2CgREb2dlEi4SLGxOdDEzTkZIS0cxUi8yaUtiR3NaK2dBbmJmT0Ridk9qZmhoZHM4UW"
        "dIREk9EjcKBUJ1bm55Ei4SLDFHZTByNjJtUUtSenBPdVZ2d3d5bXpKV1Y2QzNvMms0dmg5N0VwOXhhVUk9EjgKBktp"
        "dHRlbhIuEixJRGtQbmQxR0ZQV3dLM3g3M3RmVGI1T2d5dXpIdDlxTE05UmdMTkhYS1hvPRI2CgREb2dlEi4SLHJOcG"
        "dCR1NjdExLU0xyc1pFK2JzcHN4UnNITVRjdUlQay9kUFh0clpHd2s9EjcKBUJ1bm55Ei4SLER0YU9aOVNLMDYzb25j"
        "M0tiLzlQcHFaaG8zTXRZcDRwZ0k1bHZEZThvRDA9");
}

BOOST_AUTO_TEST_CASE(anonymousVotingAggregateHPoint)
{
    dev::eth::ContractABI abi;
    std::string hPointShare = "okC7xuEG7yVFHxY2pI+gxPdez9StkhxZrCs5diySZXI=";
    std::string hPointSum = "0PoMptw9RCM5U9D59bC76WVEOfzjYpxdHLAthYOATy8=";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_AGGREGATE_HPOINT, hPointShare, hPointSum);
    // anonymousVotingAggregateHPoint(string hPointShare, string hPointSum)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();

    std::string newHPointSum;
    abi.abiOut(&out, newHPointSum);
    BOOST_TEST(newHPointSum == "vLQ720mMmUTJ4J+0tQKinhFrxO9MQEG58Zyxtmw0J1M=");
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyVoteResult)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "Cix2TFE3MjBtTW1VVEo0SiswdFFLaW5oRnJ4TzlNUUVHNThaeXh0bXcwSjFNPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteStorageSum =
        "ElwKLG1Mc2UzSk8wTGtNR0UxeENvZFE2VXNMTlJNMVNxcDV0N0JSdXZsYkZDUU09Eix3a1FzbkQxclIwc25uc3dpV0"
        "hLaVptZk9oSy8rVzBpdU1saTVaNnZvMG1JPSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosQW55dHQwcWRQVUV6N2c3bmxrdnMzRU1uSGlreDh6VnB2VzVnMHFoSkEyND0SLFBDQlFWOW1RQX"
        "daZ2tHWkxYaDVsem1CWHd0RndtMWRJSElvNG83TzVyQm89ImQKBERvZ2USXAosWWhnWkF4NWxiMytwVjRsekV1MW1F"
        "NUpFYU01UlRZb3U0WEtJN3VRTEpBYz0SLEdLWDdRbE1Eai8wcUs2NE1OOGZNaVk2a2JjRkFvaFV1Mm5IbHhqWFFwd1"
        "k9ImUKBUJ1bm55ElwKLGNLczNqUzgxYllkTGs5d1ZETW5zSC8zdzY1N2NJMzBhR1k0aVRBL29Ld0E9EixPQ2RBWUl3"
        "ckQ0TGtPWXZMMmZ5cUQyOVdPbzRPWXp0Y1F4NHVqeFJFSWx3PSJmCgZLaXR0ZW4SXAosSER6SmlyNUU3V0NPR2o0ZG"
        "5RalhpMDQvM0Y5TUJoWW05ZEVma2w0NlJWYz0SLG9PZ2pRdkpEdjRpZVYwT0xJMGF5UEtnVWhUemRsaFZseTU3M28z"
        "SnRMSE09ImQKBERvZ2USXAosNkdNVHE5dlJmaWRaSWtTZ25wcDhGS05hcUxDYyt2NDc1MnBVREhTMG5qMD0SLEtCbl"
        "dMT3c1cjVoZ0hZM3ZDNDNEdHNJa0J2UVp4UHhMZDUrQ3JjMmhaQWc9ImUKBUJ1bm55ElwKLFZCb0k0ZEZVWlBwT0Nq"
        "NTFYWnB6cXZqUkUvRzU1dTdVSGI5NVRMYU5hUWs9Eixyc1VXU1BCLzFBdnQ0R3A5MDhLMzR5YWhFMlhxd3B1NmhRMl"
        "dHdW1wTndFPSJmCgZLaXR0ZW4SXAosK2xWWHpQQzFLVlo5bTBSVkY0R0lEVEtLY1c0MUViSXFXWWg0Q294ay9Sbz0S"
        "LDJuS3ppcmlvZWhTNW1LSFZGTTlWeE1pcTdGeUg1M1lQdnhLaHltcDIvalk9ImQKBERvZ2USXAosRVBydlBsL1RMS0"
        "p0b3QvcVBWTzBJcHVnUDA1aThVdENzLzhBMUpPSWZHOD0SLFZLTG9xVTlkbEUxcEMxZ3doZktKRFBiRjNQd3VKdzhu"
        "TGtkTGY0QVdMREk9ImUKBUJ1bm55ElwKLDNDR2toUERGVzF4Tm1LRER3bVlFZ1cveUE2QjdVYndoV3FnVmtmQ3NXeE"
        "E9Eiw1SWJ2VkF5ZisyVUoweFJNRE5ZcUEybm4zYW5IOU5xV05rZk5zZWM3VmlnPQ==";
    std::string decryptedResultPartStorageSum =
        "CjcKB2RlZmF1bHQSLFVOTkNTWVU5NXM2YW9oT3Npbms3SHdvSjRWcXZlZGNMYjBtMXlQN0orVnc9EjgKBktpdHRlbh"
        "IuEixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRI2CgREb2dlEi4SLEFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjcKBUJ1bm55Ei4SLEFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjgKBktpdHRlbhIuEixScHpFN3hXZEJ6bDJhZ1dGTytBK1VKYUI3UUpo"
        "elc0TWRCdGhKekg4TVRFPRI2CgREb2dlEi4SLHpJOFRIT1VRTlhaL290VzVZQVRWVU9KNmFsV2FzakRLRXBnMGdZaW"
        "duenc9EjcKBUJ1bm55Ei4SLElsK2pwZkhiUVE4MzNmRUNKa1N2NXd4SXpJd1h3ZzM4NURzM1hpZU1teUU9EjgKBktp"
        "dHRlbhIuEixTT3JORnVGS2JoRnNJbTMrRFMxaEc3ZHFaaVBRWGxud05Hb3crREN6ZXpjPRI2CgREb2dlEi4SLGh0Nm"
        "VBbm9PdW9vd2FrUkRMc28zb2kzbk5zNlpzZU9tNHFMUkRrS1BESHc9EjcKBUJ1bm55Ei4SLHhFektQL216djUza2Vq"
        "NTgzcFhwcVBLMHhoa2RwZlFzR2k0Sm12NitNRHM9";
    std::string voteResultRequest =
        "CkEKHgoaV2VkcHJfdm90aW5nX3RvdGFsX2JhbGxvdHMQPAoKCgZLaXR0ZW4QBgoICgREb2dlEAkKCQoFQnVubnkQDA"
        "==";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_VOTE_RESULT, systemParameters,
        voteStorageSum, decryptedResultPartStorageSum, voteResultRequest);
    // anonymousVotingVerifyVoteResult(string systemParameters, string voteStorageSum, string
    // decryptedResultPartStorageSum, string voteResultRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    u256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorSystemParameters = "123";
    std::string errorVoteStorageSum = "123";
    std::string errorDecryptedResultPartStorageSum = "123";
    std::string errorVoteResultRequest = "123";
    param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_VOTE_RESULT, errorSystemParameters,
        errorVoteStorageSum, errorDecryptedResultPartStorageSum, errorVoteResultRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingGetVoteResultFromRequest)
{
    dev::eth::ContractABI abi;
    std::string voteResultRequest =
        "CkEKHgoaV2VkcHJfdm90aW5nX3RvdGFsX2JhbGxvdHMQPAoKCgZLaXR0ZW4QBgoICgREb2dlEAkKCQoFQnVubnkQDA"
        "==";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_GET_VOTE_RESULT_FROM_REQUEST, voteResultRequest);
    // anonymousVotingGetVoteResultFromRequest(string voteResultRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    std::string voteResultStorage;
    abi.abiOut(&out, voteResultStorage);
    BOOST_TEST(
        voteResultStorage ==
        "Ch4KGldlZHByX3ZvdGluZ190b3RhbF9iYWxsb3RzEDwKCgoGS2l0dGVuEAYKCAoERG9nZRAJCgkKBUJ1bm55EAw=");
}

BOOST_AUTO_TEST_CASE(anonymousAuctionVerifyBidSignatureFromBidRequest)
{
    dev::eth::ContractABI abi;
    std::string bidRequest =
        "Cs4CCixBcEJybWswUEh4UHA0RWx2bmhsbXdFaUFrbFZkRGJYK01pY3FNTDU5MWVDMhKdAgpYRys4SUlJZFVKM1RYZH"
        "BwczJRWThvVmlrc2xkb2swYklvVUx2ZnlkWkFzUjlISzdUL1ZpSHRNNjYzTDVUTHNsUDJIUzVsWkRYenRUT1FUcEFS"
        "bURBUmxZPRLAATQyYzIzOGJhYWRjNTJlMGY5YzA1NDM5NGU1YWY5NjEwZWY1NjBmNTdkZWM4ZTA1MWY1Yjk3NGI0OG"
        "E0MGI0NjU2ODc3OTdjYjI5NTlhMTE4ZTdkNzlmOGVhZjYyOGQ2OWZmNWZlYmUzODIyMTZhMjg4OTNiMzVhYjNjOTRk"
        "Y2M3YzYzMDgwMmJhYjNjNjQ1ZTgxMzk4OTU2MjgyYWZhYmQ5NDFlNWNjY2ViYzgwMDMxMGM2YTgxNTkwNjY1Y2RjNR"
        "LKDQrAATQyYzIzOGJhYWRjNTJlMGY5YzA1NDM5NGU1YWY5NjEwZWY1NjBmNTdkZWM4ZTA1MWY1Yjk3NGI0OGE0MGI0"
        "NjU2ODc3OTdjYjI5NTlhMTE4ZTdkNzlmOGVhZjYyOGQ2OWZmNWZlYmUzODIyMTZhMjg4OTNiMzVhYjNjOTRkY2M3Yz"
        "YzMDgwMmJhYjNjNjQ1ZTgxMzk4OTU2MjgyYWZhYmQ5NDFlNWNjY2ViYzgwMDMxMGM2YTgxNTkwNjY1Y2RjNRKEDDNk"
        "ZDVhZDkwN2IwNDVmOGNhMmM5OTQyODQ1YWU5NGVhOGJjMDBiNWUzYzQ2ZjVjY2U3MGI1ODgzN2M5YThkODMwMDZhZT"
        "hjMTg1NTRkMzEzYTViNGJiMDU1OWQ4ODJlMDdjNjQwYzMzZmQ2YmMxYTYxYmI5NmZmZmM5NDFhNzRkOGQwNzgwZDE3"
        "NTgzY2Y5NGI3YjQxYTU2ZjBjMTRmNDZjYzc2OWNkOGQyNGVlZGY4OWQ2MWMyNzNkNmE3NDUxfDE5NGMxZGNkOTdkOW"
        "ViMjVkZTVhZDBmNTBjNzNlNWMyNTcxOGUwZjUyZWExZWEzNGU5MTc1ZjJkZTI5ZjZkMmVlMWNiMjI3Mzk1Y2ZjZTMx"
        "ZWUwMjRiNWYyODkyOWY1NmY2MjVmOTM3MjFmNTljMDUwZWQ3ZGIzMGRmMzM1YWVkNjg3ODFiNWQwMTU5ZTRmNjNlYz"
        "A1YWM1ZGM2MDQ3ZGY1NWEwN2YyODg4MDFhZTE4NTQ1MjVjZTAyYzc3YjZjN3wzODg4OGFhNjk1MzI5ODI4OTExODdh"
        "MTVlNmFjZjE3OGZjMjUzOTFjZDE0NzNlY2ZhZjUzOGI2NTZjMGY2NWUyNmZhNDQ1YzI3YmFkMjZiZTQyOWViYjgxYj"
        "g4NzZiYmNkNWUyOGJlMmQ0NTkwMzZiNGY0YTgyNzFhNWU3ZDhmNzVmYjViYjNjYTI0MWU4NmNhOTJmMjRhZDZmNmZk"
        "N2Q5Y2RkNDI5MTNkYjBkZjkyMWE2NmVmMzQyZDEzZjZhYmV8MTJlOWFhZTQyNmQ3Njg1YTY4ODBlNmJjMTJkY2U2Yj"
        "liMWMzNjRhMmUyYTVkZjNjOWYyY2U0OGYyNDgzNGJlMzFlZDNhY2JhOTQyYWFlODY5YzI0MThhYTdmMDVkNjhmYzM3"
        "Yjc0MTVlYTc5MzM3YjczYjRiNjAyN2RiNTdkM2EyYzlhNTE4YmQwMzQ1M2M0OWY4MTE4ZDE1OWNjMTY4Y2Q4ODdiOW"
        "QwNjZhNjQwODg1NWZlMDVlZDdiYzQwYWQ0fGZlNjZlNGM3NzhhZjRjMTFlZTRhYjA3MzBkYTQ0MzRkMDEwNGU5YjVh"
        "NzljN2QzNjY4MjRlMDc0OWEyMzVkZjBmYzZkZGQyNjczMjAyMmI0N2E3ZjcyZjE0YjM5NDg0YmNjNTA0NmMzZjZjZT"
        "RiZTVlNmUwMjBjNjU3YjAxZTU5NTM1ZDQ5NmZkMzg2OWZmZmU1NzAxOWM3YTEzZWM0YThhZjUxN2FiNmI4MmY0NzQx"
        "NmUyNzdjNWY4NDdmNDk4fDJiMThlMTZiMGM2NzJmNWM0YzVkZjM5NTkzYTIwNzlmZWI4ZGM1Y2I5MDc5MzBiOGE5ZW"
        "Q3Y2ZiMzRjMmJhNjRiOGRmOGQ1NjQwZmE4YjYyOTg5OTQ5MjY3ZmFkNWVlMGY1ODUyNGFhNzhkOTM3MTRkOWVhYmE0"
        "Y2M5MzE1NzQzNmJjYzU3NWJkODVlYjc3OWMyNTg4Mjc1MTY4YjYzYjg5MzRkZDM0ZjVhMDgxNWI4OTY5MmExMDc0Yj"
        "E2YjY4OHxlYWVlZDQxOGVhMDQ5YjhjYzc2MmFjYzYzYTdjMTYxYjc3ZTQzYmUxZWRlYjNlNWIzNmIwZWFhOWQ2ZmU1"
        "YjU0YTk3NzZiMjUwN2I4ZDc0NjZmZTNhZmQ1Y2FlYzRmMzdkMjc4ZTRmYjY5ZDc3MzY3ZDUxZTRhZWYzYTVhZjNhOD"
        "kyZGI2ZDljZTk5NGZhMGIwNzE4ZDJmNTU4ODNkZTA2M2U4M2Q2YjRmMjU0NWE3MjBhOWMxZTM4ODRhYWUwMnwzYjQx"
        "YjNkYmJmMzBlZWY1YjJhZjA0MjhmMTNhYjJlZjg0N2ViMjE3MjM4YzViMzJhNDZiODNmYzRlN2Q2MGMyODFjOTY5Yz"
        "c5YjUwNTkzZDkyMTBkZTg1NDkxNGM2ZDRkOTgzMjdmNzM3ZGYxMzdkZjJhMTU4MjA0NWIzY2M0YjhjMDgyZDllNTEx"
        "MTJlYmIxYTBjMzJiYjI1NzUxYWIxZWNhMWZkZmQ5YzBiZWY3MGE5MGJkYTJmYTk3NzQ3OTA=";
    bytes param =
        abi.abiIn(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST, bidRequest);
    // anonymousAuctionVerifyBidSignatureFromBidRequest(string bidRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    std::string bidStorage;
    abi.abiOut(&out, bidStorage);
    BOOST_TEST(
        bidStorage ==
        "CsABNDJjMjM4YmFhZGM1MmUwZjljMDU0Mzk0ZTVhZjk2MTBlZjU2MGY1N2RlYzhlMDUxZjViOTc0YjQ4YTQwYjQ2NT"
        "Y4Nzc5N2NiMjk1OWExMThlN2Q3OWY4ZWFmNjI4ZDY5ZmY1ZmViZTM4MjIxNmEyODg5M2IzNWFiM2M5NGRjYzdjNjMw"
        "ODAyYmFiM2M2NDVlODEzOTg5NTYyODJhZmFiZDk0MWU1Y2NjZWJjODAwMzEwYzZhODE1OTA2NjVjZGM1EoQMM2RkNW"
        "FkOTA3YjA0NWY4Y2EyYzk5NDI4NDVhZTk0ZWE4YmMwMGI1ZTNjNDZmNWNjZTcwYjU4ODM3YzlhOGQ4MzAwNmFlOGMx"
        "ODU1NGQzMTNhNWI0YmIwNTU5ZDg4MmUwN2M2NDBjMzNmZDZiYzFhNjFiYjk2ZmZmYzk0MWE3NGQ4ZDA3ODBkMTc1OD"
        "NjZjk0YjdiNDFhNTZmMGMxNGY0NmNjNzY5Y2Q4ZDI0ZWVkZjg5ZDYxYzI3M2Q2YTc0NTF8MTk0YzFkY2Q5N2Q5ZWIy"
        "NWRlNWFkMGY1MGM3M2U1YzI1NzE4ZTBmNTJlYTFlYTM0ZTkxNzVmMmRlMjlmNmQyZWUxY2IyMjczOTVjZmNlMzFlZT"
        "AyNGI1ZjI4OTI5ZjU2ZjYyNWY5MzcyMWY1OWMwNTBlZDdkYjMwZGYzMzVhZWQ2ODc4MWI1ZDAxNTllNGY2M2VjMDVh"
        "YzVkYzYwNDdkZjU1YTA3ZjI4ODgwMWFlMTg1NDUyNWNlMDJjNzdiNmM3fDM4ODg4YWE2OTUzMjk4Mjg5MTE4N2ExNW"
        "U2YWNmMTc4ZmMyNTM5MWNkMTQ3M2VjZmFmNTM4YjY1NmMwZjY1ZTI2ZmE0NDVjMjdiYWQyNmJlNDI5ZWJiODFiODg3"
        "NmJiY2Q1ZTI4YmUyZDQ1OTAzNmI0ZjRhODI3MWE1ZTdkOGY3NWZiNWJiM2NhMjQxZTg2Y2E5MmYyNGFkNmY2ZmQ3ZD"
        "ljZGQ0MjkxM2RiMGRmOTIxYTY2ZWYzNDJkMTNmNmFiZXwxMmU5YWFlNDI2ZDc2ODVhNjg4MGU2YmMxMmRjZTZiOWIx"
        "YzM2NGEyZTJhNWRmM2M5ZjJjZTQ4ZjI0ODM0YmUzMWVkM2FjYmE5NDJhYWU4NjljMjQxOGFhN2YwNWQ2OGZjMzdiNz"
        "QxNWVhNzkzMzdiNzNiNGI2MDI3ZGI1N2QzYTJjOWE1MThiZDAzNDUzYzQ5ZjgxMThkMTU5Y2MxNjhjZDg4N2I5ZDA2"
        "NmE2NDA4ODU1ZmUwNWVkN2JjNDBhZDR8ZmU2NmU0Yzc3OGFmNGMxMWVlNGFiMDczMGRhNDQzNGQwMTA0ZTliNWE3OW"
        "M3ZDM2NjgyNGUwNzQ5YTIzNWRmMGZjNmRkZDI2NzMyMDIyYjQ3YTdmNzJmMTRiMzk0ODRiY2M1MDQ2YzNmNmNlNGJl"
        "NWU2ZTAyMGM2NTdiMDFlNTk1MzVkNDk2ZmQzODY5ZmZmZTU3MDE5YzdhMTNlYzRhOGFmNTE3YWI2YjgyZjQ3NDE2ZT"
        "I3N2M1Zjg0N2Y0OTh8MmIxOGUxNmIwYzY3MmY1YzRjNWRmMzk1OTNhMjA3OWZlYjhkYzVjYjkwNzkzMGI4YTllZDdj"
        "ZmIzNGMyYmE2NGI4ZGY4ZDU2NDBmYThiNjI5ODk5NDkyNjdmYWQ1ZWUwZjU4NTI0YWE3OGQ5MzcxNGQ5ZWFiYTRjYz"
        "kzMTU3NDM2YmNjNTc1YmQ4NWViNzc5YzI1ODgyNzUxNjhiNjNiODkzNGRkMzRmNWEwODE1Yjg5NjkyYTEwNzRiMTZi"
        "Njg4fGVhZWVkNDE4ZWEwNDliOGNjNzYyYWNjNjNhN2MxNjFiNzdlNDNiZTFlZGViM2U1YjM2YjBlYWE5ZDZmZTViNT"
        "RhOTc3NmIyNTA3YjhkNzQ2NmZlM2FmZDVjYWVjNGYzN2QyNzhlNGZiNjlkNzczNjdkNTFlNGFlZjNhNWFmM2E4OTJk"
        "YjZkOWNlOTk0ZmEwYjA3MThkMmY1NTg4M2RlMDYzZTgzZDZiNGYyNTQ1YTcyMGE5YzFlMzg4NGFhZTAyfDNiNDFiM2"
        "RiYmYzMGVlZjViMmFmMDQyOGYxM2FiMmVmODQ3ZWIyMTcyMzhjNWIzMmE0NmI4M2ZjNGU3ZDYwYzI4MWM5NjljNzli"
        "NTA1OTNkOTIxMGRlODU0OTE0YzZkNGQ5ODMyN2Y3MzdkZjEzN2RmMmExNTgyMDQ1YjNjYzRiOGMwODJkOWU1MTExMm"
        "ViYjFhMGMzMmJiMjU3NTFhYjFlY2ExZmRmZDljMGJlZjcwYTkwYmRhMmZhOTc3NDc5MA==");
    std::string errorBidRequest = "123";
    param = abi.abiIn(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_REQUEST, errorBidRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousAuctionVerifyBidSignatureFromBidComparisonRequest)
{
#if 0
    dev::eth::ContractABI abi;
    // std::string bidComparisonRequest = "";
    bytes param =
    abi.abiIn(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST,
        bidComparisonRequest);
    // anonymousAuctionVerifyBidSignatureFromBidComparisonRequest(string bidRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));
    u256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorBidComparisonRequest = "123";
    param = abi.abiIn(API_ANONYMOUS_AUCTION_VERIFY_BID_SIGNATURE_FROM_BID_COMPARISON_REQUEST,
        errorBidComparisonRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
#endif
}

BOOST_AUTO_TEST_CASE(anonymousAuctionVerifyWinner)
{
    dev::eth::ContractABI abi;
    std::string systemParameters = "EAg=";
    std::string winnerClaimRequest =
        "CsEBMTYzZjdiMzc0ZWIxNTJhZDhmMGVlODA4NGQwMzgwMGRlZjE1Zjk4NzkwNGI4NmUzOGY1NGQxMTUxMjFjZDk2Y2"
        "VmYTJmZTYzMTJkNTZiZWU5MzFjZWMzMGNhZmU2NDYwYTc2ZTRkNTkwYmUzMDQxNTVkMWIzODZmMDZkOTA3NzNhN2Y0"
        "NzJiNTQxMTgxMWE3NTFhNWIzMTVmOWRhOWZiMjc1N2Q3ODQwNTNmNGE3NDBhOTQwYzIzMmRkZWFkYThlNRLFAQphMT"
        "llNTI5ODNmNTI4OGMyNmRkZWM3ZjYwZWI5MjhjY2MyNjljYTRlNzFhYzM4OGM4ODc5ZDg0MTU4MmMzM2Q3ZDAwYjA5"
        "OTNlNGNmYTJkOGRkN2MyMzlkZmE3ODEyNjIyMxJgZGJmMTlkZDc3ZWJlNjIxNGY5NGFjM2ZhNTUwNTdkMWI2NzY1Mj"
        "RjMmE2MDdlNjhjOWQ3YTMwMGQ1YWQ0YWJiNTc0MGI3ZDhhNzhjZWY3NjlmZGQ1Mjg4YzE0OTdlNTU3GAE=";
    std::string allBidStorageRequest =
        "CtANCsEBMTYzZjdiMzc0ZWIxNTJhZDhmMGVlODA4NGQwMzgwMGRlZjE1Zjk4NzkwNGI4NmUzOGY1NGQxMTUxMjFjZD"
        "k2Y2VmYTJmZTYzMTJkNTZiZWU5MzFjZWMzMGNhZmU2NDYwYTc2ZTRkNTkwYmUzMDQxNTVkMWIzODZmMDZkOTA3NzNh"
        "N2Y0NzJiNTQxMTgxMWE3NTFhNWIzMTVmOWRhOWZiMjc1N2Q3ODQwNTNmNGE3NDBhOTQwYzIzMmRkZWFkYThlNRKJDD"
        "ExNzUyOGVjNDYxZTkwYWNiYmZlMmRhNGY4MTU4ZmZkNzBhMTcxNzY0ZDg3ZjAyMDVlNDcyZGNkZmI2NjQ1OWE2MzY1"
        "YmU1NmU5ZjA3MmZhYTAwZDYyN2MyMWM1NDRkN2YzMWE1OTY3MTBkY2E2MzAyYjJlN2NiY2I0Yzc4MjFkNmIyOWIzMD"
        "JhNmY3MmNhMGU3ZTg1ZTdhNTJiMDViNGQ0MTVjZTdlMmIwZDM2YTdlZjEwM2VhNjk4NTQwNTU3OTJ8MTA4OTdhZTRl"
        "N2RjNGZkYTFiNDg3ZjhhNzJmODJhNTFhMzlhNTBmYWIyMzJlYjA5NzIxYmY1YzA5OTExMDc3ZTJmNzY0YzM1OGI0MG"
        "VmZjkzMDMwZWUwYThhNjIxMzQyNzI5MjA5MGJmNjdlZGUwZGFkODVhYzg4NjU2Y2Q4ZDE4ZDJmMDhjNjk5OGFjNDEy"
        "MGEwYjJlNzI2NDQxNWI3ZDRiMjc5N2JhNzEwNjk5MmYxNWNmZjhmNjQzZTllYjA1NnwxMGM5MmUxMTczYTI1NmFmMz"
        "AzNWQ4OTQwM2MxOTM4ZTUxMzJkYjc1ZWFiNThiM2YxYjU4ZDUyZTAzNDAxMDRjYWJlMmEwNGU4YTBmZWI2ZWNlMDY1"
        "MGRiNzY0NDFlNjU0OGIwYTE5YmRhNmJjYjE1ZmQ0M2VkYzM3NTQzN2UzYWRiMGEwOTJiZDI2NDlkYmRjNTY0M2FmNT"
        "djMTU5NGUyYTY3ZGQ5OGU5NmYwZTg2NDY1OThlNDVhNGRmNTg3Y2U5fDNiMTYyNDNiZmNjMDYyMWUwNDM1OWY0ZGY0"
        "ZTA3ODdmOTRjYjE4MzgzNmM2NmFlNjhmZDdkZjVkYWQ0Y2Q3NDgxZDJmYTc4MGYxMjdhMzhhNjNmMjQ5MWZmY2I3Mj"
        "k5OTA4NDI1N2IyZGFmNjI0YmFiMzFkMDAzYjYzMDlkYTJhYTA2ZWIwMDlmZWU1MmQxMTkzZTU3ZGY1MjI3MTdkZjll"
        "M2E2NDVhZmM5YzczZDM5YTAxMzI1NDAzMTk2MjkxYXw2YTg0OTQ2YTQwZDcwZDk0YWFhMzZmMDFlOGJjNjBhMzk3Mz"
        "llODFiNDdmNWNkZjNiY2JmMmIxNzJlYmEyZDY0ZjhmYWY2NGZkOGYxYTQxYjBjYmNhYjFlYTc5Y2Q0ZDhmNjUzNmZm"
        "NDE1NTViNzRkOGZmOTRiZmQ0ZDM2ZWUwNWViMzM4MjAxNDE1NDU4M2M2NDlmMDg0OGRjMjUwNmVmYzk4ZjA3ODRmMD"
        "U0NDQxZjliMTA1ZGEyYzUzZWNkMDN8Mjk4MTNlNDgyOWE0NTk0ZjJkNTM3ZDIyZjRkOWYxMTZhMmE4NjQ4MmRhNTRk"
        "MzgyZmM3YTA3MDM3YzRhYjJkYmY1MzAyYzAzY2E2ZTkzZWI3NmIzYzQyZjMzMmI4NGVhMjI0ZjMwOTY0Y2NmNTMxNz"
        "Q3ZmNhODM1MDBkNzE4ZjJjOTY0MDBhZWJkNGYwYjRhMjU3ZGVkNDIzNTgwZDM3Njk3OWEwZjQwYTkzZGFhMjMwMTNk"
        "YmU1YmE0ODUzYzc4fDQ4NDEwOGRjZTJmZmVhYzYwNzk4NmQyMDViNzRkZmI3MTUyNzNlYzllZmRhYjk5ZWU4MWYyZm"
        "MwODQ4YmFkMWE3Y2Y4MTZhZWJiOTQ5YTM2OGM3MGQ5ZTkzMTVjNWMyNjAxYmNlZmY0OGFiNzQ2MGQ4YWY2ZDM4MThh"
        "MDFkNGFkYjg1YjFjNTYzODMyMDA2OTZjODVkMmI5NjIyZjBiZmFhMjRhMzE1MTdkYTIxOTAxMTI5MGQ1ZGNhZWMyMm"
        "Y0fDFlNmIxNGRkMGNmZDJiYzY4ODRkMzk3MDNmNDFmODc1Y2FkYmQ2MDllMGRlYzYwN2QxZDUyOTZlNjMzYjUzNTVi"
        "MjlkMzllMTVhMmU2YjdjNWZiNzhjYTEyNDJlOTg0NmQ2MjNkNTQ4MWEwMGNlMjQ3ODY2ZTgzMDYwNTRjZGY2NGRkZm"
        "YyODY2YzVlMDRkMTk2ODhlMTk2OTM2NjVlMTEyMmQzZDcxMTQxOTViMDdjZjU5M2EwNzQ4ODYxMDVmMQrMDQrAAWMw"
        "Y2JkNjNiZWYyNDQyZTBiM2QwMzZhNjFlZGViNzJlMzg5MjU5ZTJiYjVjMWUwNTUyMmY3ODM5ZTZmOGQ0MzdlZTg0Zm"
        "FjODNmY2Q0ZjU5NTNlMjM2NTExMTMyMzA1NWU2NzMxOTIxNzQ2Njg3MjhlYjhlOWY2ZWIzMDk0OGJkOGU3YjAxYzgx"
        "MGExNjE0OTEzZTM2YmUyN2FiYjk0MWIwZmJkMzY5ODgyMmFjNjUzZTMyZTQxNWI3OTdmMTExMRKGDDg1MDMwMDE0Zj"
        "QwYjcyYWRkMjMyOTdlN2ZjYTgyNTI3ZmQ1NTcwNDUzMzczYzEzYmRiNGZhMmY1MmViNWJkOTdkOTliZWI2ZWVmOGFl"
        "NTIzZjc3Njc5MDE3ZDIwNDYzYjAxYzljOGI1Mzk0ZGIxNGVhYmQ0MzllNTk3MTk4YWEwZjM2MjZjNjNjZTI1YjkxMT"
        "hiNWVhMmY3YmQ0YzI5ZDM3NWZkN2Y3NTM0ZDY4MGU1M2VjMjY4ZmQ4MjA2NDE5YnwyNTBlMTUxZGQyZmUxNGExMTM5"
        "Y2NhMDJiMjUxMWFhNjM2YjRlMzMyY2U2ZjIyODc2ZjQwOGI4Y2JlNWM0NGZhMzgwNzllMmI5OTRiNjE3NDU2OTNmNG"
        "I5NTU0MDc0YmFhODdjMGI4NDk3YWFmYmI4MGFlYmRmYTYzODYwZGFlYmQ0OTg1ODdkMDUwNzgyNDA3ODhiZmRmMTQ4"
        "YzlmMmMzNjQwZjI1NzYwZTIzMGZlZDRlZjNkMmVkYTE3N2ZiMmZ8N2JjZWY0YTY5M2FhNTlkMTIwZTQ0MjdkNTM0ND"
        "NiOTAzODJhZmQ5ZGQ4MGYxMDhiMTJmMjBiNGM3Y2NmY2ZmZmU3ZGRjNGJhOTc3ZTg1OGRhZWQ4Mzg3MDk4Zjc2NmNk"
        "NTFiYjYzYjhmZGYwMGVjYTY5NTFiY2FmYjNhNzFmNGJkY2Q0MzEwMmJiN2E3YWFhYjFjOGQ4ZDA0MTJkMGFmOTMxMG"
        "FiNzIxMzZiYTc1NGZmNDA4YzIyYmVlNjNkNmZjfDM4YTNkMDk4ZmZjMWYzYmRjZjllZDIyNTAyNjM2MDA1ZDRkNGI3"
        "MDAxNzQ0OWEyZDkzYmVhMTVjOGY5NmIwOGU2ODE4OWNlMWIwYmFhMGJmYjlhNGVkNjdlMGVmN2Q3NGIxOWM0NDk5OD"
        "VhMGEwNjdhMDEyZmUwZmRhYzExN2MxZGRiN2I2YjYxOGVjNzQ0ZjdlNDYwN2ZhODgxODgzNDkxZThmNmNiZGViMGQw"
        "MTc4YTQ3MTIwNjE2YzRhOGRmZXw0YTliMDBjNWQwMDY3ZTFkNjNkMTIxZmYyMjU4MDg5NDMwZGQ0NWYzMjNlYjEzMz"
        "JlZmFmOGU4ODRiMDNiMDBkNTkyYmRhZTJmYzc4MjcwZjI5NDBhYTg0YzQ4NTZiNzgxZmI0NTgzZWU4OGFkNmRjMDZj"
        "YzIyODU4YTcyZTk2NmVhNWQwNzZhN2ExZWM0MGRiZjQxMDY5Njc1ODI1MzRhYTc2NWI0NmI5N2VkOTZmMTY2MGM1Yj"
        "c2OWVlYTI0ZTB8MTBkMWQ1YTc1OGIzOTQ2NDJhYzJhMTBkMjg2NjJhOTk0ZjlkNmNlYzI4MTJkNGFjNmE3MGZkNzE0"
        "ZmZkOGM5MmIzMTEzOWMzMThhMTY1NzhkOGQyODBlMDIyYmUwM2RiNTRiMzU1M2Y0M2FjYzBiZjUzZDU5NTE1YzI1NW"
        "VmYWM1NGI3NWQyZGNkMTFkMmUxMTgxZWNkMzQ4ZGMwZjIzNzAyMjk0Y2EwMjMwNDJjMWQyMjk0MDY3MjU0OTlkNzY1"
        "fDNlY2UzMDAxY2Y4ZjgxOGRmZWFhM2YxMzZmZjg0YmY0YmRmZTBhNTg2N2VlMTJkNmIyOTc4YTZjNGJhMjE0MmVlNj"
        "kwMmI4M2E5ZmZlMDc5YTcyZWZmZGUzMDNjNmE1NThmNDUzYzdmYTQ1OTJjZTRhNWI3YzY4ODg3OWQyMWQxNDZhY2M3"
        "OTI5OWE5MmI0MDAzZmU2N2JmOTAxODIxMzJlMzY2NzVkYWVkZDg1ODkyNmJkNjhkMzkzZWZhODQ5fDNmOTk3NTZlMT"
        "g5MGZkYTM1MDRjY2VkMDA3YmE3YzNmNThkNDViZmE1MmM0NzM4ZTdkYjQzNGMzODgzNTY4YmQzMWZiY2M3M2VlNTU4"
        "MGNkNDQwZmM1ZjFkOWQxZTEyYjcxODI2NDRhMDkxNTIxZGY4ZjQzOTIwYTU1YjcyNTJiZDQ5MGJjMzY3NWE2MDk0Mj"
        "E1ZDQ4ZjU3OWY4NWZjYTg1OTI1YTdiYjI2MTk5NGY3MjMyMzJhYjBkNGJiNDQ1Mw==";
    bytes param = abi.abiIn(API_ANONYMOUS_AUCTION_VERIFY_WINNER, systemParameters,
        winnerClaimRequest, allBidStorageRequest);
    // anonymousAuctionVerifyWinner(string winnerClaimRequest, string allBidStorageRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    u256 bidValue;
    std::string publicKey;
    abi.abiOut(&out, bidValue, publicKey);
    BOOST_TEST(bidValue == 1);
    BOOST_TEST(publicKey ==
               "163f7b374eb152ad8f0ee8084d03800def15f987904b86e38f54d115121cd96cefa2fe6312d56bee931"
               "cec30cafe6460a76e4d590be304155d1b386f06d90773a7f472b5411811a751a5b315f9da9fb2757d78"
               "4053f4a740a940c232ddeada8e5");

    std::string errorWinnerClaimRequest = "123";
    std::string errorAllBidStorageRequest = "123";
    param = abi.abiIn(API_ANONYMOUS_AUCTION_VERIFY_WINNER, systemParameters,
        errorWinnerClaimRequest, errorAllBidStorageRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(confidentialPaymentIsCompatible)
{
    dev::eth::ContractABI abi;
    std::string targetVersion = CONFIDENTIAL_PAYMENT_VERSION;
    bytes param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_IS_COMPATIBLE, targetVersion);
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    s256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorTargetVersion = "errorVersion";
    param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_IS_COMPATIBLE, errorTargetVersion);
    out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_FAILURE);
}
BOOST_AUTO_TEST_CASE(anonymousVotingIsCompatible)
{
    dev::eth::ContractABI abi;
    std::string targetVersion = ANONYMOUS_VOTING_VERSION;
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_IS_COMPATIBLE, targetVersion);
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    s256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorTargetVersion = "errorVersion";
    param = abi.abiIn(API_ANONYMOUS_VOTING_IS_COMPATIBLE, errorTargetVersion);
    out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_FAILURE);
}
BOOST_AUTO_TEST_CASE(anonymousAuctionIsCompatible)
{
    dev::eth::ContractABI abi;
    std::string targetVersion = ANONYMOUS_AUCTION_VERSION;
    bytes param = abi.abiIn(API_ANONYMOUS_AUCTION_IS_COMPATIBLE, targetVersion);
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    s256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorTargetVersion = "errorVersion";
    param = abi.abiIn(API_ANONYMOUS_AUCTION_IS_COMPATIBLE, errorTargetVersion);
    out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_FAILURE);
}
BOOST_AUTO_TEST_CASE(confidentialPaymentGetVersion)
{
    dev::eth::ContractABI abi;
    bytes param = abi.abiIn(API_CONFIDENTIAL_PAYMENT_GET_VERSION);
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    std::string version;
    abi.abiOut(&out, version);
    BOOST_TEST(version == CONFIDENTIAL_PAYMENT_VERSION);
}
BOOST_AUTO_TEST_CASE(anonymousVotingGetVersion)
{
    dev::eth::ContractABI abi;
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_GET_VERSION);
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    std::string version;
    abi.abiOut(&out, version);
    BOOST_TEST(version == ANONYMOUS_VOTING_VERSION);
}
BOOST_AUTO_TEST_CASE(anonymousAuctionGetVersion)
{
    dev::eth::ContractABI abi;
    bytes param = abi.abiIn(API_ANONYMOUS_AUCTION_GET_VERSION);
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param))->execResult();
    std::string version;
    abi.abiOut(&out, version);
    BOOST_TEST(version == ANONYMOUS_AUCTION_VERSION);
}
BOOST_AUTO_TEST_CASE(unKnownFunc)
{
    // function not exist
    dev::eth::ContractABI abi;
    std::string split_request_pb = "123";
    std::string errorFunc = "errorFunc(string)";
    bytes param = abi.abiIn(errorFunc, split_request_pb);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(wedprPrecompiled->toString() == WEDPR_PRECOMPILED);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_WedprPrecompiled
