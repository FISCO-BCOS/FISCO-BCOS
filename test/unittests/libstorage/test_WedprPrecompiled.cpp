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

        tableFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        wedprPrecompiled = context->getPrecompiled(Address(0x5018));
    }

    ~WedprPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr wedprPrecompiled;
    dev::blockverifier::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(WedprPrecompiled, WedprPrecompiledFixture)

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyIssuedCredit)
{
    dev::eth::ContractABI abi;
    std::string issueArgument =
        "CtUCCrQBClhCSElnNjFCMXA5SU4zSmk2aHJOYnNGbnV0YUVUNDMyNnBOREVaVTUvdmFNR0NhdTFydE4ydFByRG01Sz"
        "hCUElYR011VUd5U1hFRkNPcFVNc1ZwWHZydXM9ElhvY2xoSU9JQ0VyV2FJbUErZ1dPdHcyV1BqNUZKSUxiNnd6RGZR"
        "amxObTF4d2hpUytBYjA5YnZYM25BMktQOC9UY1JpMFJTcFFYUUxiNGFia2NoWWJPQT09Egg1ZDllYTI4MhpICiw5Qm"
        "ZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5L2RRSTJvUVlrOVRBWVZjdz09"
        "IkgKLDlCZkpUYWo0aC9PaWNzbk0wY0dBY2U4Y01JUXJVdG1kZlpFWldhQ1hJUW89EhhwYVlMS3kvZFFJMm9RWWs5VE"
        "FZVmN3PT0SLHRMbVBuK1ZMWEsrTW5NZ3Q4L0Rsei9DNEc4bmMydVROdnlwUWdXRHcyeVk9GgIIZA==";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT, issueArgument);
    // hiddenAssetVerifyIssuedCredit(string issueArgument)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string currentCredit;
    std::string creditStorage;

    abi.abiOut(&out, currentCredit, creditStorage);
    BOOST_TEST(currentCredit ==
               "Ciw5QmZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5L2RRSTJvUVl"
               "rOVRBWVZjdz09");
    BOOST_TEST(creditStorage ==
               "CrQBClhCSElnNjFCMXA5SU4zSmk2aHJOYnNGbnV0YUVUNDMyNnBOREVaVTUvdmFNR0NhdTFydE4ydFByRG0"
               "1SzhCUElYR011VUd5U1hFRkNPcFVNc1ZwWHZydXM9ElhvY2xoSU9JQ0VyV2FJbUErZ1dPdHcyV1BqNUZKSU"
               "xiNnd6RGZRamxObTF4d2hpUytBYjA5YnZYM25BMktQOC9UY1JpMFJTcFFYUUxiNGFia2NoWWJPQT09Egg1Z"
               "DllYTI4MhpICiw5QmZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5"
               "L2RRSTJvUVlrOVRBWVZjdz09IkgKLDlCZkpUYWo0aC9PaWNzbk0wY0dBY2U4Y01JUXJVdG1kZlpFWldhQ1h"
               "JUW89EhhwYVlMS3kvZFFJMm9RWWs5VEFZVmN3PT0=");

    std::string errorIssueArgument = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT, errorIssueArgument);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyFulfilledCredit)
{
    dev::eth::ContractABI abi;
    std::string fulfillArgument =
        "CtUCCrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFpDWl"
        "BkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydXpuV3BPWHZR"
        "SGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1ZDllYjU3MxpICix5S2"
        "tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXBpYnlLQVlVUT09"
        "IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s4aDg9EhhJL253RklTYVQ5U2liWm1BL2"
        "hBaXlRPT0SjwErM3gzTDZDWFR2Qm1OeFFZM2drV1hRaXJHM21aMm1ocjE3cmxYZkh0dHdZPXwrM3NiRUkwdGd4MVFP"
        "RWVvSDRTNUdBTkh0NjlLNlNMRS9Pc2QxWFZLTHdNPXxPUG5nc05OUUhtbzN1YkpBbnVMY0NpcnkxT2VzOXFDUTlMZW"
        "8vRzJDamdNPXw1ZDllYjU3Mw==";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT, fulfillArgument);
    // hiddenAssetVerifyFulfilledCredit(string fulfillArgument)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string currentCredit;
    std::string creditStorage;

    abi.abiOut(&out, currentCredit, creditStorage);
    BOOST_TEST(currentCredit ==
               "Cix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXB"
               "pYnlLQVlVUT09");
    BOOST_TEST(creditStorage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNK"
               "QUhSL0dENXBpYnlLQVlVUT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");

    std::string errorFulfillArgument = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT, errorFulfillArgument);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyTransferredCredit)
{
    dev::eth::ContractABI abi;
    std::string transferRequest =
        "CtUCCrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFpDWl"
        "BkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydXpuV3BPWHZR"
        "SGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1ZDllYjU3MxpICixjTW"
        "ovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJU2FUOVNpYlptQS9oQWl5UT09"
        "IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s4aDg9EhhJL253RklTYVQ5U2liWm1BL2"
        "hBaXlRPT0S1QIKtAEKWEJBOVJPV1VaRWIvdi9wbGYzdzRReGlXOTk5ZkZTQzFYc0NhV0lUQ3NiU2ZXelpITm9nRkhW"
        "N0NsWkNaUGRqQXZ4ZkdzVWFsSFY4cFZCRENWZ3ZzbG81UT0SWDBEZFlyaTdwdU9KcHBpUXdWVDZjTDdYZTFwaDJ1em"
        "5XcE9YdlFIZVhaSUZ5ZlR2R29RdTNKUXFIWS9XU2dxeVFOOUNpY0duL055cWdLeW1lR0NvS2V3PT0SCDVkOWViNTcz"
        "GkgKLHlLa1AwYWcrZFJad3d1dkR1S3FqVURLcVBaeEZIS05uMnU2RHE0RTB6RTA9EhhDRDhqU0pBSFIvR0Q1cGlieU"
        "tBWVVRPT0iSAosY01qL0lzakNiV0FVdUtHR3JWVktqYXl4NG1BT1BGdmdQMlR4VWRzSzhoOD0SGEkvbndGSVNhVDlT"
        "aWJabUEvaEFpeVE9PRr4AgosZGtXRDlXcnpVek9aVmhlN29NQXBxWTM2ZnVQMEFoenlwcWhndFFwL0IwUT0SLFdHSE"
        "kvZDhwTGRObGF6NEJXWFlxWW5Ba0s2eW5jMk80dW5kSWVvZnBlQjA9GixUaDRjNWlKU0VKQ1NnL2h1UTRiVlZ2STA0"
        "aEV0ZTFUZmowZmhHek83WmtjPSIsWUVWTVF0YjkycUZ1Vk5lajM2Q2p4Q1NYRVB1YXQrQkp6Y1dCejh0UU1nST0qLC"
        "9zNUVjcm5GeGk4NUVCcmp5SjVaQS9JMDRoRXRlMVRmajBmaEd6TzdaZ2M9Oo8BUUljVC9Fb21VWFBzeUdYaFZIYVZI"
        "alhTMmxkbW9ubTFJMXpkRmpPaTNRbz18Z3Jma3VsQTU1RHJPT1IrdE9PdDBKTEs5UlM1WnVGREFQZ3ZVeHEvd1lRUT"
        "18VllWVGQ3QU9SU3Bic0o3RzJ2Y1VTbUZrMnVKeFZTWmgzWGo0eE5YcG1Bbz18NWQ5ZWI1NzMiCHRyYW5zZmVy";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT, transferRequest);
    // hiddenAssetVerifyTransferredCredit(string transferRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string spentCurrentCredit;
    std::string spentCreditStorage;
    std::string newCurrentCredit;
    std::string newCreditStorage;

    abi.abiOut(&out, spentCurrentCredit, spentCreditStorage, newCurrentCredit, newCreditStorage);
    BOOST_TEST(spentCurrentCredit ==
               "CixjTWovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJU2FUOVNpYlp"
               "tQS9oQWl5UT09");
    BOOST_TEST(spentCreditStorage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICixjTWovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJ"
               "U2FUOVNpYlptQS9oQWl5UT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");
    BOOST_TEST(newCurrentCredit ==
               "Cix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXB"
               "pYnlLQVlVUT09");
    BOOST_TEST(newCreditStorage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNK"
               "QUhSL0dENXBpYnlLQVlVUT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");

    std::string errorTransferRequest = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT, errorTransferRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifySplitCredit)
{
    dev::eth::ContractABI abi;
    std::string splitRequest =
        "CtUCCrQBClhCQnBUMnRKZnI5c3QzVzFYcUZaZWJrdlRKRWRycDJpSDBJMnJVM005b1dUYlBYcFBDZjF2SWJ5bkJOZG"
        "16NkIzdVNGLzlJZFVVNGdOZXhXL2NQa1BvTE09ElhsWEtucHQ3NEZUZXNYNDArVEkwY0s5aVNkMU5oUEltcVR1Smhl"
        "S09UOUY5Qno4NUNzYUl2Q3hGeldIeVFXemdhQVRKQVJTSm1QNmEvNmtnY1R6NVZyQT09Egg1ZTAyMWI2MRpICixiQk"
        "xZREIzWFNRU082cXlHOUdCb0tleTVZWStSQlIyaUxESHNOWnNIZGxNPRIYbzB2Ulo0M2pSNVdYVFhHQ0xmb2x2UT09"
        "IkgKLGJCTFlEQjNYU1FTTzZxeUc5R0JvS2V5NVlZK1JCUjJpTERIc05ac0hkbE09EhhvMHZSWjQzalI1V1hUWEdDTG"
        "ZvbHZRPT0S1QIKtAEKWEJCcFQydEpmcjlzdDNXMVhxRlplYmt2VEpFZHJwMmlIMEkyclUzTTlvV1RiUFhwUENmMXZJ"
        "YnluQk5kbXo2QjN1U0YvOUlkVVU0Z05leFcvY1BrUG9MTT0SWGxYS25wdDc0RlRlc1g0MCtUSTBjSzlpU2QxTmhQSW"
        "1xVHVKaGVLT1Q5RjlCejg1Q3NhSXZDeEZ6V0h5UVd6Z2FBVEpBUlNKbVA2YS82a2djVHo1VnJBPT0SCDVlMDIxYjYz"
        "GkgKLDVqbG1KVUhLSURKQkJZYU1VQjBrZ3U1NWlOZ2dsLzI4eGJUbkhUdFZTQUU9EhhZajN1TFNxVFRsaXcvOWVRVF"
        "NkczhBPT0iSAosYkJMWURCM1hTUVNPNnF5RzlHQm9LZXk1WVkrUkJSMmlMREhzTlpzSGRsTT0SGG8wdlJaNDNqUjVX"
        "WFRYR0NMZm9sdlE9PRLVAgq0AQpYQkJwVDJ0SmZyOXN0M1cxWHFGWmVia3ZUSkVkcnAyaUgwSTJyVTNNOW9XVGJQWH"
        "BQQ2YxdklieW5CTmRtejZCM3VTRi85SWRVVTRnTmV4Vy9jUGtQb0xNPRJYbFhLbnB0NzRGVGVzWDQwK1RJMGNLOWlT"
        "ZDFOaFBJbXFUdUpoZUtPVDlGOUJ6ODVDc2FJdkN4RnpXSHlRV3pnYUFUSkFSU0ptUDZhLzZrZ2NUejVWckE9PRIINW"
        "UwMjFiNjMaSAosaUYrelV3UDlmaGJ0SDRja2ZKOTRNUXJSWGRaeEczNUtEYW8xbE80MlUxaz0SGGU5aWtjSjRjVDBL"
        "KzRzRGRyYnJpSnc9PSJICixiQkxZREIzWFNRU082cXlHOUdCb0tleTVZWStSQlIyaUxESHNOWnNIZGxNPRIYbzB2Ul"
        "o0M2pSNVdYVFhHQ0xmb2x2UT09GrMPCskHCixZa2g5aGtyK0djRTdZSlVVWTlQQWtBNXlXeHJuY2VBWHlXQzhRUC80"
        "WEJvPSIsWkRkRnRKblN6SjBOTW95L1hwTkxmdjlwcyt4Vm50YW9DTlgxYkV3WDJBVT0y2AVBQkd5UjR0bzlDbzJ2cT"
        "Q1RzNuYy81dEZINVZpdTlROXNkZFkwOWlySnhYR0NGSzM1TmZnZTc1WXBKR3hBZFZPUHA0akN6NEdIYjNkMW1YM0NX"
        "QnpEZWlERjRXUWlCcGwwZVV4enBxUVVxM2ltRWdIbFlpMmNFSzQ0Umg5cm1jVDJoVGViRm45a2UrN01zTEpMb2Z2SG"
        "tUeGQzK0VNUFdJbHVNTDN3emVHVjZtOFVwMGkxZG1za2sxRDNIUmdqTE9HaUlqSkp4ekthMjJGQnV0WDJiMENSQ2lw"
        "bU56Vk9ZQ3BQMm00RW4vU3lxbXRFdXMwYS9xazh0WmNya2xBMzBQenBGaDNKR0ZEUDFBVGQ3T1UwYzg4eHZvV2ZpY2"
        "VJSEtTdDJ6aHpFV0xneWkwWnZJVDBhT0RaMDdkc29rbXArQXQ2WUFCTUovMGVaZzJRT3Bzb2NnVFNTdkdaR2s2em1t"
        "ejJheVZTa3dVcTE1U0ppalI3VXRndUJDREUweXcva1BYdTA4UWVCbEZaTnNNOTgzRUlDRGZGQ0FLVFkyMkFkVC9Sdj"
        "hOYThUdUFHeS8zdVVaZEl0OTJXcUc0MmRrSHlITGdBSWlEd1U0TGxEbWdIL3RjVXZZQjUzZVJtN1VoRHhXYU9kdVN3"
        "bllUbWRCb0UzTkNjWExzNHBNMi9RSVV4QU1oK3lSSWY5eFVEdjJseXJyMTczSnlPSTkvaXhZamxsaFlKMXVPM2xyR0"
        "IraUF3UW1pSnRJN1lzL2k3d0FtTzJiNFFyOHptcncwd203WjdhNnRWS1AzTGVLVkl1Y0xtd0d2ekZ5cWJpaHJMUy9L"
        "UVdCK0k3elFkeHFMa2N2dHhQdWFwenQ1eDE4Q0gxRTd6aEdsZTBKMjVUOHIzRWFrN3hUMmtyeTg0ckdBRlAzWTFWZy"
        "twMjBTaWE5cE1xNUFBY1VyVDJ4YThJdkFNT1JtcXNpVDBDRFE9PTqPAWZMdUwwdHczN1BSMVZYc09NRldNcVhBcDJo"
        "d0VnTSsveGYxOE9iWlByZzQ9fEpia3F0TTdENktNamc0bC9uSTBaejc5c29BTzg0b0FYclJxYTVLcFRSQW89fGc5Rn"
        "AxZytPSHBiYUhRam5XZ1Z2TktOS0dOL00ySi9BeVd2VlRzU1Jxd0k9fDVlMDIxYjYxEt0HCixWSnBmcC9TdEdUUHR5"
        "dzYzOHluT0Z2Z0tZemJ6MmNpWjlmWEw3VUxFZGxBPRosUHQyeVoyK3EvWnFBci9RNW9RaEdCNUZMUkVNL3k0Y21IU3"
        "hGQzFYSHFTWT0iLHNHbHUrWVNOd3NJR1I2dENYclJuNEdteDZlME4xT0NOMmNVQlRMUGFPZzA9KixvRFhIclRyazJP"
        "clRkUVgwNHhTSTNaQkxSRU0veTRjbUhTeEZDMVhIcVFZPTLYBWxtM1g1V1E3S205TjU1RzY0M0RETzc0enlFanVadz"
        "huRUdaZExqTW1ZWCtzNTNVQ0pRRVNTazk4NHhaaDFSaURqTVExMnJjZjAzWTNTSmpSUC9WcUZheWtkaFZDcjV3alFz"
        "ZmtvWlRjUDI0WG9RUWNRL0tZNVk2ckVNQ0FKRHRTc21lY1BENnBtQ2d5bkJQM2I4Q1BKdHNVMFRMYVdRYXNwd1ZtUW"
        "xwZ3dHSFZZNTVxWDVpNCtxanhtbXgrRmxUTmNXaG9oZWZGOG93YXF4aFVMN3dPRDAvVkFrcXJoME9Rb1hIaW4weUFC"
        "UEN1bVh1QnVPcTFaZ1p4SVVoM3Uyc0hucWY0YmVMcFBrL1lrT0s5a1A5UEdaa0ptSDJ5UFF0VFNlbFRDeWxnNFE3SV"
        "cvWWdsbHBzTExZSGRFUVNvTFNmYmxlcDVBS3lzMjUvclV5VDQ4QUxaSHBKYVZ1OFc5RG53OERmNXgzNFZiUnVhV2JQ"
        "d2g1Q3c0UnRZOHpraHlka2tncEV1Z0hiRmFZK2JPZ0xGdGNHS21jcFFRRmJFUHN2Rk5tcy9lSW9uVGlZN2tDUTVFU0"
        "kyRC9QQ1JkS2MzMDlaWGNnL0l5d1grdDBDdXBXL3pyNkt4UXJOWnlMaEdRTnptMVNlaGkvSGxxWXVFQzRPYjRYQkNS"
        "eXJCcVV5QUlUOW1YZVBLT1hLUC9ndHluanF0UTBORUZTaDNhL3B2SmNOWUVqUFI1R2UzbUtqSnRRWEk2T3diMnd2R3"
        "dNaFVlUEl0cTdRSG1ZaXhRWGNDaFRFNDhhYlp6MXB2L1ZZazZKWDMxS1pEYmhuY1pGUURGd0NycnJnYVlVSU9TVGFt"
        "TjNGckY4MHVTOTh2NURzcSt1NUNkelBtbGQ4aWpITFQ4NDJ4bGtrVDNrY3dyVFYvNDE4ajl1TU1TZHJXbVFwRXlNaU"
        "JtQUlkKzVGY2VNV3ZKeDB0SVlCUT09QkgKLDVqbG1KVUhLSURKQkJZYU1VQjBrZ3U1NWlOZ2dsLzI4eGJUbkhUdFZT"
        "QUU9EhhZajN1TFNxVFRsaXcvOWVRVFNkczhBPT0aBXNwbGl0";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT, splitRequest);
    // hiddenAssetVerifySplitCredit(string splitRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string spentCurrentCredit;
    std::string spentCreditStorage;
    std::string newCurrentCredit1;
    std::string newCreditStorage1;
    std::string newCurrentCredit2;
    std::string newCreditStorage2;

    abi.abiOut(&out, spentCurrentCredit, spentCreditStorage, newCurrentCredit1, newCreditStorage1,
        newCurrentCredit2, newCreditStorage2);
    BOOST_TEST(spentCurrentCredit ==
               "CixiQkxZREIzWFNRU082cXlHOUdCb0tleTVZWStSQlIyaUxESHNOWnNIZGxNPRIYbzB2Ulo0M2pSNVdYVFh"
               "HQ0xmb2x2UT09");
    BOOST_TEST(spentCreditStorage ==
               "CrQBClhCQnBUMnRKZnI5c3QzVzFYcUZaZWJrdlRKRWRycDJpSDBJMnJVM005b1dUYlBYcFBDZjF2SWJ5bkJ"
               "OZG16NkIzdVNGLzlJZFVVNGdOZXhXL2NQa1BvTE09ElhsWEtucHQ3NEZUZXNYNDArVEkwY0s5aVNkMU5oUE"
               "ltcVR1SmhlS09UOUY5Qno4NUNzYUl2Q3hGeldIeVFXemdhQVRKQVJTSm1QNmEvNmtnY1R6NVZyQT09Egg1Z"
               "TAyMWI2MRpICixiQkxZREIzWFNRU082cXlHOUdCb0tleTVZWStSQlIyaUxESHNOWnNIZGxNPRIYbzB2Ulo0"
               "M2pSNVdYVFhHQ0xmb2x2UT09IkgKLGJCTFlEQjNYU1FTTzZxeUc5R0JvS2V5NVlZK1JCUjJpTERIc05ac0h"
               "kbE09EhhvMHZSWjQzalI1V1hUWEdDTGZvbHZRPT0=");
    BOOST_TEST(newCurrentCredit1 ==
               "Ciw1amxtSlVIS0lESkJCWWFNVUIwa2d1NTVpTmdnbC8yOHhiVG5IVHRWU0FFPRIYWWozdUxTcVRUbGl3Lzl"
               "lUVRTZHM4QT09");
    BOOST_TEST(newCreditStorage1 ==
               "CrQBClhCQnBUMnRKZnI5c3QzVzFYcUZaZWJrdlRKRWRycDJpSDBJMnJVM005b1dUYlBYcFBDZjF2SWJ5bkJ"
               "OZG16NkIzdVNGLzlJZFVVNGdOZXhXL2NQa1BvTE09ElhsWEtucHQ3NEZUZXNYNDArVEkwY0s5aVNkMU5oUE"
               "ltcVR1SmhlS09UOUY5Qno4NUNzYUl2Q3hGeldIeVFXemdhQVRKQVJTSm1QNmEvNmtnY1R6NVZyQT09Egg1Z"
               "TAyMWI2MxpICiw1amxtSlVIS0lESkJCWWFNVUIwa2d1NTVpTmdnbC8yOHhiVG5IVHRWU0FFPRIYWWozdUxT"
               "cVRUbGl3LzllUVRTZHM4QT09IkgKLGJCTFlEQjNYU1FTTzZxeUc5R0JvS2V5NVlZK1JCUjJpTERIc05ac0h"
               "kbE09EhhvMHZSWjQzalI1V1hUWEdDTGZvbHZRPT0=");
    BOOST_TEST(newCurrentCredit2 ==
               "CixpRit6VXdQOWZoYnRINGNrZko5NE1RclJYZFp4RzM1S0RhbzFsTzQyVTFrPRIYZTlpa2NKNGNUMEsrNHN"
               "EZHJicmlKdz09");
    BOOST_TEST(newCreditStorage2 ==
               "CrQBClhCQnBUMnRKZnI5c3QzVzFYcUZaZWJrdlRKRWRycDJpSDBJMnJVM005b1dUYlBYcFBDZjF2SWJ5bkJ"
               "OZG16NkIzdVNGLzlJZFVVNGdOZXhXL2NQa1BvTE09ElhsWEtucHQ3NEZUZXNYNDArVEkwY0s5aVNkMU5oUE"
               "ltcVR1SmhlS09UOUY5Qno4NUNzYUl2Q3hGeldIeVFXemdhQVRKQVJTSm1QNmEvNmtnY1R6NVZyQT09Egg1Z"
               "TAyMWI2MxpICixpRit6VXdQOWZoYnRINGNrZko5NE1RclJYZFp4RzM1S0RhbzFsTzQyVTFrPRIYZTlpa2NK"
               "NGNUMEsrNHNEZHJicmlKdz09IkgKLGJCTFlEQjNYU1FTTzZxeUc5R0JvS2V5NVlZK1JCUjJpTERIc05ac0h"
               "kbE09EhhvMHZSWjQzalI1V1hUWEdDTGZvbHZRPT0=");

    std::string errorSplitRequest = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT, errorSplitRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyBoundedVoteRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixNaWdLNG5vNHFZdWFaZWNQY0JsMmRoYlRNOWtDOWtxMTRpV3pHcGJtZUhvPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteRequest =
        "CvoECrQBClhDdXVZQlErMm5KQVlIN1laK0Fhd3U5azVBOUdtYjl3N2d0TFp0OG9PemtrdE1LdHJoV1J1czRGVVdXN2"
        "U2NzI0a1JFTDE5QWM5Nnc2ZUFYVEhvb3lkQT09ElhCSEJ4Zm05K01aMjJzbWt3WTRUV29EVVEwYlgxREx6NmRpRTVj"
        "T25hOWhxdFl3ZjBNc2MrQ2t0NWxsa1RHbzUwWnRCRG4rYlpscU1kemVyUnhSNG9wNkU9ElwKLE5yV3pKNXRQbXBQRH"
        "Y2UzR1NExDSmhRV2VnK1VPUExiWjVlTkNldFlyRE09Eix3a3BLeGg2N1VZQmM5bGJURjd5WnlNNVBFMU5IeDZ4elFa"
        "ME5Ia2p2L3hRPRouCix6Q3JhNENqb3hxSFFUSDJpN3FSM05rTEZQMnlWdnN4L2ZvS3E4YkNxZlNjPSJmCgZLaXR0ZW"
        "4SXAosMUptTnBrZ09mZ3Y3NVk3Y25GbGlNSXNhd1Q0NkEvTFVlelNEU09Ibjl5Zz0SLGx0VC9tUnJYZVpJNkszOERP"
        "eTA0RVBDREI0QzNCV0hLQnpnQjFXaWszUzQ9ImQKBERvZ2USXAosOEdGMlBOWmV3bHVxN1BQQmVZb1NIRm5Cb3lnWD"
        "VNeGJCRUpDL2FYTExSWT0SLENEckF0M29BYmsvNUs3U2JzN2ttdUhLd2FCSlRBU2liaTNrd1VmeEhBMWs9ImUKBUJ1"
        "bm55ElwKLFpzTXRtMXB1ZXYwQnBLTndTdUg0bmJ1QnhZR2lyR1Q1ckhadWgvdTEyQ3c9EixWUGRpaXRyNHQ2bzFzQU"
        "JVT3JMZmJVbVREYndXbThNTXNIbFZ6Z0c5UjA0PRKYAQoGS2l0dGVuEo0BCooBCixQTFhDTnJlSTNmRWNybUZpbjB1"
        "ZGgxVDBENVorWXlyLzd0WUhRS2ZDVGdvPRIsVldXQUN0ZXNSRzhGWWdQRnIrUmFZckFzYjFRbDVTNlV2M0ZwNHRlYj"
        "dnbz0aLEFxZ3NnRHN3RjZ1Vjh0Tno1cjZPYWFDK0p2N1JvRUM5ajhWMXJQSkVwZ1E9EpYBCgREb2dlEo0BCooBCixj"
        "OW9jWEpHa3BTVUVTM0k1SkY2d3B5eUI5QWNnREJMY0NmelRveW1BR2dFPRIsdEl5NDltT2R0VGVEQ2M5Nkg3UXRMN2"
        "tLRmNDSkN6NnZzSlpuYnIyeVJBUT0aLGFub1BHclBkOEJjSkUxY013b1AvMkdrQ3lhRU1vd05aSnJ1QXFpb2JBQUU9"
        "EpcBCgVCdW5ueRKNAQqKAQoseVFmMDlUb0tzaEVlUE1aUEMxTjdaZFpYN2ZlRFppOTlydnMyRnZmZ1Z3Zz0SLEY3WU"
        "pqZ3BNQU9XWlh4ZDlGcGpjMWRKL01RNVBTZ3VnUkgxVHpQUE5hUUk9GixNNkJhb1N5VE5VRGFTdVR0ckV4dVo2ZFBm"
        "U09mbFVndlZ5SkYwZ0JEUmdrPRqABzFKOUhIR08zMXBRWDlWQUd6bXZibGlZYmpoeEJWVXVtbUpnbitjSHdLUVppeG"
        "ROUHNPQmpJWHZ6M3ZpZTZtcENPdXduSFQ5RVNoVG5xLytwT2gvcEZ1Q05xc1VUMUd2YklQbGlHWVQ1WlRiQmZKZnRE"
        "V3RRTTVZbU9zeUg0SHhpZ3FIeHFxeUt2anZzVXlzdm5hZnJ3MENrNzJ3ZGNoM3ZPdVFiUmZTTFhHTVpDMUN4REVPZ0"
        "xqYy9CSEhkSlY2TmhRLy9iSU9UL0ZvcU1uOUdKYjFMRFBIcnd5MnZGRHdNaGpMQWlxTzlIaE9NbW1pNjVHQm9YcVdi"
        "enJIS0lKY0p5c0VFYVREcHd5YWZocHF6VWJWQk0wUklsM0lFb1hJRE1wclVmS2xxN1F6a1RBNDBEY1pwelpuWlA4a2"
        "tCVjNJbGVOYzZDRXdWMlh1TWxBV3RXWUtFcHh1WTRUME1BZTJuTyt3ckNrVkQ2SXFUTU9pdnUxcEF0eDNrTkwwdFg1"
        "MGlCL09uN2crNFlJUEx3NERRWFpTSU5qWlZlemxYSG5tdjJoZXRsNFB5QnBNc1ZVQmJNWkdpS2g1RnY3Q2RvVlRTd2"
        "dJbFBFc2VJU0o2Uk9hZlpNZVUwcVh2bi9haitQOHlRR2RwU3Z1SGk3TEs0ZTlSS2RpV2hJemQvUWh3ZUpXMHRadVVw"
        "M1lLa0d3ZldYZWg1US9IR2drVXZjMWFJdkdBbCs2QWg4S25GZEtWeW9vc2hGWWlaR01lMDhqa0RLK0RZUnNCd2xOeF"
        "pWMDVmWWtzNGJDQldTRXNydGQ1R3hQSm9SdThSYWdJU0tPbkFpUTR5Q1JFUzhNTnlXTVIxbzJFb1RBOURDNlM2cGlv"
        "bFJKOFBiOWFpK0J4L1NkQU1xZmE4a0VkZEdROGd5RWc3WnBuc0t4VHFxUFJGVTBOeVZxU0grVzlVaSs5aHh4dU5KbE"
        "FzRDNEZ3J4VnpPeTd1SHp3dG1hVG9peGRHanIvWURpTmRQL1BGN3ZPOEZsYzRZM2hLVjVOK250RGhWMXpGSDFXbUtI"
        "T1d1a0crbHBWeEhiTU5BeGdXT3pZR0pidStMQjRQT0VGdE9vNGFhS2JvTHVjNC9pUTNONWIwTlNrdnhwVnNWb0JNTH"
        "pQZ0h4aWxCNW5sVVBsTUtnYlYwRUJ0d0ErWlFmNkl4cVNKUm8vVmdEIpQCCixpbWl4N1hVZGN0aVVjdENmSXJvdnVp"
        "VXlqYzZhVTRSMTBoK0xSOWlHaHdVPRIsVUFYUW14UEdpY0RYS0xjWDg1WmVNQzNZd3oyUEhuR3JMZW1YamErdDRnTT"
        "0aLGd5cTFKY04yRXM1QnduaU13SlJmZ2gwUFlhM3pMQlN4TW1BUERFY2VxZ0E9IixmUWdOdHd1WnBEOUphUjY1WGxz"
        "QVZSY3NxQmtmNDBUdjh5dFFUVTZZNGdrPSoscWUyUFZkamxwQ3QrODF0Y28wN2pOZkZTSU5abTIxYlRPRXUvV203Ry"
        "93WT0yLFQzOFl0eU00VE52Z2RaU1RKSDNjSHptV0tBZ1FvdEZzRUJKVWhmZFAyQW89";
    bytes param =
        abi.abiIn(API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, systemParameters, voteRequest);
    // anonymousVotingVerifyBoundedVoteRequest(string systemParameters, string voteRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string blankBallot;
    std::string voteStoragePart;
    abi.abiOut(&out, blankBallot, voteStoragePart);
    BOOST_TEST(blankBallot ==
               "CixOcld6SjV0UG1wUER2NlM0dTRMQ0poUVdlZytVT1BMYlo1ZU5DZXRZckRNPRIsd2twS3hoNjdVWUJjOWx"
               "iVEY3eVp5TTVQRTFOSHg2eHpRWjBOSGtqdi94UT0=");
    BOOST_TEST(
        voteStoragePart ==
        "CrQBClhDdXVZQlErMm5KQVlIN1laK0Fhd3U5azVBOUdtYjl3N2d0TFp0OG9PemtrdE1LdHJoV1J1czRGVVdXN2U2Nz"
        "I0a1JFTDE5QWM5Nnc2ZUFYVEhvb3lkQT09ElhCSEJ4Zm05K01aMjJzbWt3WTRUV29EVVEwYlgxREx6NmRpRTVjT25h"
        "OWhxdFl3ZjBNc2MrQ2t0NWxsa1RHbzUwWnRCRG4rYlpscU1kemVyUnhSNG9wNkU9ElwKLE5yV3pKNXRQbXBQRHY2Uz"
        "R1NExDSmhRV2VnK1VPUExiWjVlTkNldFlyRE09Eix3a3BLeGg2N1VZQmM5bGJURjd5WnlNNVBFMU5IeDZ4elFaME5I"
        "a2p2L3hRPRouCix6Q3JhNENqb3hxSFFUSDJpN3FSM05rTEZQMnlWdnN4L2ZvS3E4YkNxZlNjPSJmCgZLaXR0ZW4SXA"
        "osMUptTnBrZ09mZ3Y3NVk3Y25GbGlNSXNhd1Q0NkEvTFVlelNEU09Ibjl5Zz0SLGx0VC9tUnJYZVpJNkszOERPeTA0"
        "RVBDREI0QzNCV0hLQnpnQjFXaWszUzQ9ImQKBERvZ2USXAosOEdGMlBOWmV3bHVxN1BQQmVZb1NIRm5Cb3lnWDVNeG"
        "JCRUpDL2FYTExSWT0SLENEckF0M29BYmsvNUs3U2JzN2ttdUhLd2FCSlRBU2liaTNrd1VmeEhBMWs9ImUKBUJ1bm55"
        "ElwKLFpzTXRtMXB1ZXYwQnBLTndTdUg0bmJ1QnhZR2lyR1Q1ckhadWgvdTEyQ3c9EixWUGRpaXRyNHQ2bzFzQUJVT3"
        "JMZmJVbVREYndXbThNTXNIbFZ6Z0c5UjA0PQ==");

    std::string errorSystemParameters = "123";
    std::string errorVoteRequest = "123";
    param = abi.abiIn(
        API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, errorSystemParameters, errorVoteRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyUnboundedVoteRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixGaWxyOG1vc3dtMDZSQ1J2clZCVGF3YlhXMFNvS01xRjJNeXRRbk1FdVVjPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteRequest =
        "CqgFCrQBClhDbjEzRjNJNVpYbHZQK3N6QWpHVklzSHFST3FuUGVDSXB3UVFUaFR0bkhzdUJ0eTdrVlRzM2dkTFBFcH"
        "VnM1hGV1VOZmZxbER1THRadG9VallraUlWZz09ElhCQ1ozaHRSdlpOSVgvVkVacUZxenNFWk5pZnl4SVpMamM3ZXJB"
        "cnFGTGM3UktpNWJWR1c5VU1xaDR1bHprODg1OGhlTVVOUUZwdlVwbEFzbC9KOXhIVEU9ElwKLHJ2UUtXcnB0bVYyRE"
        "Y4NWZLbDhNNkpOcS9HMmFqYXhjT1hIQThZTnJzbHM9EixkR1RSL1FsUnQ5cUxwSnZydlRyeTZ3S09lc29LTmg5bnR3"
        "OU9IYXU1b1c0PSJmCgZLaXR0ZW4SXAosN3NBOExzazZvUWxsc2lxaE9rZ2VHQms4TXdsblpSTnZBYVdHaW5WNDFFTT"
        "0SLEdMb2d2SmtaeS9ZeVV3U3RKeVBYbFVaMHcrQ1JpUEk3ZGg0R0FRUVlxM2M9ImQKBERvZ2USXAosdGowb0ZWVHRi"
        "Um1xS3dpVVJOM1VuWUtWSkJSdEdIS2VxY09BR0VFMmxCTT0SLDRGNFBVeVIvU0M3ZVVFQjRaWHljNk83dmo3Z2NKTH"
        "Awa3lhcVNqK0lNblE9ImUKBUJ1bm55ElwKLHluWU50YmFhWTBaUHh0NmEvUHNrc05TVVMwaS81VFNyekxSd3FRVWor"
        "eHM9EixVRGUvSkFTeUhsZWZGbWRaQUFaNjltblA1ZnZKWFVWRjVzSTNpMjFkWmhRPSpcCixBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRIsQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQT0SiwQKBktpdHRlbhKABAqKAQosbXBxYkR3dTRPanZSd1pCeHFVcXJTMmZmY0h4R3BSOTFLNG82ckNaSk"
        "53QT0SLCszb2dVTlVzb1llelFGYTVKT0JBWXlBOVVVSFZOb0k2aWFFUnJ3K0YrZ2s9GixuVjZUWHpKWXNHbHlRT3Ur"
        "bnVpWDIxVTArTnBLZFR2Rm80aEtOS2tlUUFJPRLwAgosVThydGd0dU8wMElGNXFYRis3L2kwaHFicXRjeFdUVlBoOX"
        "BQTllhK1FnVT0SLDJyemVHZk9aNVU3VkVjQlBNUkpBMFovOXU5ZkJXUmNSUmlIZGtmZHJkQTg9GixHQnhUV2xaeWRY"
        "Uy9EUXNZM0xqTXdZaEpERXJGTFdCNU0yN1NvMjA3TEF3PSIsaEZoVW81NjE5NTlHMGN0WWtTTFhCR2NxNUNBS2RZRi"
        "9GZm5qUlF5Q3JBQT0qLFhPVWJIV1ZoNSs0NVg2T3RacU1BNms3b3FSMVlQS242TDZzNlBtN1JaZ3c9MixaNitOMTBG"
        "Qnd6dUwxZW9HL3VBT1JPbGhMNjZ2UEtZMy9QakJOOS84akFNPTosSllQS3lzWmU4aGZFcGh4Zyt3VkNiYktscHRNTT"
        "BZNERvRWFBVStlMFRRaz1CLEtDSjN0SXlUM2swWDBWTWZKRFFIVUlQa28vVGUxVnlUMEUzdlNrYjFCd3M9EokECgRE"
        "b2dlEoAECooBCixxWWxQaUtpR2lpV2Q3OVUxQm9oZThpa0NWZzdxZW95bXI0SmRrejgrQVFFPRIsNE1DUTFPbW5BZG"
        "xUb1Q5ck51SXRmbzZpaE1RR2R0R3luQjFFbWF5UTF3VT0aLFFUNmZIRkNuU0pGVmJBUE0yZ0dHeHc5SktXWEtmT1hS"
        "cDJKT1VnbDN4Z1k9EvACCixoUHZrUTdzYndrRk9rdmY4T0ZOaEdERjR4OWo2TnRpQ01jN01IMHZLOUFzPRIsdHhtYX"
        "NkdkZtRXh2alBRVERpSmU4bHRKdjFOaEo3bXdqZFNiU01BZHBnOD0aLElKOFFCc08wQjRnNmhPZDVkL04zMDI0VGI2"
        "NjRMbnBrWFJ5THVUcmUvZzg9IixtbnMzb1pjVTVHaW1RWTNTam9WVVpocEFUcWYzMDJyU0M2MW5sQkgwU0FzPSoscz"
        "REMTNBcEhRSElNNTNhS3BXNExLcTcvNldwbm9FQ0c2Uy9RMFZRczNnRT0yLDRwV0xldm5TRUZ6SGNzbkZZQmRnYlVh"
        "eDlsRE8yWDVKOXVuRG5ZWTlSQXc9OixTa015UDQyQUgvS0djMEZzM0ppVmg0aTFtQytpRExhNTE4SEZsdFlnNndnPU"
        "IsUUdZRlBRamNnVXduZkxsM3EreEJLRDFtZ3c1U0JGK0tZTHl4Rkhsb2p3QT0SigQKBUJ1bm55EoAECooBCix5Nkkx"
        "bDc1Vm1GTDhJVDBvcGNTWmNtQ2tNamJiQXl6VzljaTZjelpSeXdZPRIsYU12eDlkVmlkbkh3TEhHMkl0UDJFWDMwaH"
        "poaEI2dXJ1V1JVSlhCcFNRcz0aLE96V0ZtU21BOGZ5OHVobnB4ZjdQZXJ4bGhXSVJVQUdXdTdXNlN4R0hUQXc9EvAC"
        "CixSRVZ1UkF4Y0ZuWjB6RXE0NHBGMzd4MVhHb2NoSGI3RlZ6U2dzMzdvQ3dVPRIsamtzM3FQK242YVBhM1VIWEVxdH"
        "kwd0MwL0hFRFovWHFTenJKYStzU1dBVT0aLHdhY2wvbWY1bElxcGp6Yzhad05kWnhFU2RQUGJrUUw4M2xES3F4N2or"
        "UTg9IixUSmlLUzd3UGFTdktiU0o1S2YycUNIUStwVmhVdm05Q2EzdlBZN0VXZGdRPSosam80bzVYN0Z3RDhyV2ovMW"
        "p2cGFaY0dodlI4SGFqYWg1bC9oUGdXdGR3RT0yLDQ4NkpQVWtDbmh3VFZpRzQrVmNmYXBISDRnTWdSOFdHdzczU2Nl"
        "ZEdhZ009OixSR1d1djRRdU84d1d6dzFNRnZUdHVqcmt3SVBPQUJnb2cvRTkxVkZzaHdvPUIsUk5kcG1adndna0FjYk"
        "RudTlOWGNyT0ZLK2JyenVHZlp6aXpUVFYwQTdnaz0=";
    bytes param = abi.abiIn(
        API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST, systemParameters, voteRequest);
    //  anonymousVotingVerifyUnboundedVoteRequest(string systemParameters, string voteRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string blankBallot;
    std::string voteStoragePart;
    abi.abiOut(&out, blankBallot, voteStoragePart);
    BOOST_TEST(blankBallot ==
               "CixydlFLV3JwdG1WMkRGODVmS2w4TTZKTnEvRzJhamF4Y09YSEE4WU5yc2xzPRIsZEdUUi9RbFJ0OXFMcEp"
               "2cnZUcnk2d0tPZXNvS05oOW50dzlPSGF1NW9XND0=");
    BOOST_TEST(voteStoragePart ==
               "CrQBClhDbjEzRjNJNVpYbHZQK3N6QWpHVklzSHFST3FuUGVDSXB3UVFUaFR0bkhzdUJ0eTdrVlRzM2dkTFB"
               "FcHVnM1hGV1VOZmZxbER1THRadG9VallraUlWZz09ElhCQ1ozaHRSdlpOSVgvVkVacUZxenNFWk5pZnl4SV"
               "pMamM3ZXJBcnFGTGM3UktpNWJWR1c5VU1xaDR1bHprODg1OGhlTVVOUUZwdlVwbEFzbC9KOXhIVEU9ElwKL"
               "HJ2UUtXcnB0bVYyREY4NWZLbDhNNkpOcS9HMmFqYXhjT1hIQThZTnJzbHM9EixkR1RSL1FsUnQ5cUxwSnZy"
               "dlRyeTZ3S09lc29LTmg5bnR3OU9IYXU1b1c0PSJmCgZLaXR0ZW4SXAosN3NBOExzazZvUWxsc2lxaE9rZ2V"
               "HQms4TXdsblpSTnZBYVdHaW5WNDFFTT0SLEdMb2d2SmtaeS9ZeVV3U3RKeVBYbFVaMHcrQ1JpUEk3ZGg0R0"
               "FRUVlxM2M9ImQKBERvZ2USXAosdGowb0ZWVHRiUm1xS3dpVVJOM1VuWUtWSkJSdEdIS2VxY09BR0VFMmxCT"
               "T0SLDRGNFBVeVIvU0M3ZVVFQjRaWHljNk83dmo3Z2NKTHAwa3lhcVNqK0lNblE9ImUKBUJ1bm55ElwKLHlu"
               "WU50YmFhWTBaUHh0NmEvUHNrc05TVVMwaS81VFNyekxSd3FRVWoreHM9EixVRGUvSkFTeUhsZWZGbWRaQUF"
               "aNjltblA1ZnZKWFVWRjVzSTNpMjFkWmhRPSpcCixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
               "FBQUFBQUFBQUFBPRIsQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0=");

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
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string voteStorageSum;

    abi.abiOut(&out, voteStorageSum);
    BOOST_TEST(
        voteStorageSum ==
        "ElwKLC9oMlNUNlNEaU5qSlBndWsyN0ZVQkJaRE5uQWtkTmU0SEF2ZStRci8yMTg9EixGc2RMUjVIbkxXMTV2WjdNVm"
        "swbWhmQmFWNStEaVJQUlVUNUQ5WVdnMEV3PSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
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
        "RndE9zaHdrPSJmCgZLaXR0ZW4SXAosOGswaFdSTGU4bkppQTVMV2VXeFprcUVQcUdnWTNkZFZtcnpBcFdtWWdtZz0S"
        "LDZnL1ZJSHk2OXRxUnd0bWdpRFV1cmg1Ym9oVlczejQ0YWpmam1HS2dwbE09ImQKBERvZ2USXAosQkx1QnQvR2JRRE"
        "RydlJENlkwMjNnc3JFMWFEdlhvUHZ0R1FVTTRDME5UOD0SLHNtQ0hPMjlLMXRqY1d0RmRxTm04Z1prbTUwUjh2UlV5"
        "ZnNvWDFENy9laW89ImUKBUJ1bm55ElwKLFRxejNaNVJGUStzcStxdG1XVXhoNzhlZ0FPTG5tYytBMFV0Y09hTUx2U0"
        "E9EixPcFBPMzlZc0Z6dTBwbTJ1bHQzN2ZZK0htREFpUFZ6ZFcwRGxxVk1oNGdvPQ==");
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyCountRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixxcWk4QmMwaXNYczZjRk1nWkl2ZEw1M3A5ZWM5c1Fqc1d5RWZQSUdZUkE4PRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteStorage =
        "ElwKLDFuZlcyWDBpT3FPTjEwTHBOMnkzSnZlTVJaaU9DY1pzUk1yaUt4YVExbW89EixRampqaDhkWFRiZnBLKzZ1TX"
        "ZzUkh0amZEMjhNUjVPV0RIRDN2MmkvY1FVPSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosckE1OXdMY21JbWt3YmlZWHNBUWhnVG1FYkhaM1hwQVA4R3ExampDSGhsYz0SLE5wZWc4TjNsWG"
        "xpaUxaWjlrSFdvZUljR1ZoQXViWVJZdlBVU0c2cDAwRTg9ImQKBERvZ2USXAosaW5nRWVaRy9oSzZlR2wyZlJoNTZK"
        "ZytyYnN0cmh0S1Y4eUFtMWhGRy9sTT0SLGRMOW9tN0h6OFk1SHVDV1pJUUFnZHY3RUJQTzVzV0NIcysyU0N0TjN4d2"
        "89ImUKBUJ1bm55ElwKLG1JV2I5eHRVaE9oQVE3cEkzZGltdktBcUJPb3d6aUFHMlpmWlVodkFrWDA9EixoaWhNOUIr"
        "cTFQRkdNTnZRTnJ4Y3ZVTUluUzBwUnBvdDFGNGtkRXgwTHhZPSJmCgZLaXR0ZW4SXAosdUdHUmZrZ0Y4QkVML3VjZj"
        "crT09kc1Y0UDNENEhDUGo2UURNVWRXR1pEQT0SLGpxTHFQTDZlM0VqVWtRQW0ycFh5ZmtsNERhV2IwRFUzdHFlVWd5"
        "NUhCaFk9ImQKBERvZ2USXAoscHFrRjJMbWdaRENTeTFFaytlTWo5Q1JaU1FqSE9TSlZaZnU2RzJBS1puMD0SLFlHZV"
        "VuQUlYRk95dzJQcGxXWjVVa2N0cHhXNGp4VVljbDQxbGQxRmluRFU9ImUKBUJ1bm55ElwKLHFyMm9RckQrRGluaXJF"
        "R0kyZHUzVHZqejJmaFlFKzVKbnV2RC9KQTZmQUk9EixrdWQ1dmFHeFdCWVFFdWpDSnBVVUhDYnNEd0NoWmZhcStMWW"
        "JWV2NLb1JrPSJmCgZLaXR0ZW4SXAosYW5oemkwYXpMYTVnZXIxYzJYRDRpeTl0SGw2ZTl5VTFGK1l6bjZadVZCOD0S"
        "LE5tMnE3Tzkrc0Q3SHdoMFNpT25IdFovZURyMnBVdkxualg0Nld3OWx1MDg9ImQKBERvZ2USXAosaGtvZ2lTZnRiSF"
        "B3WFBPY0Yrb1AwczZuaElZK29WQWlFek5rUDhYQ1lYZz0SLGRIc2FkL1ZWaGdMRXI5WTdGUWtHUVhlWmVIQStOTTlT"
        "UEpwdXJ6R1ZlUVE9ImUKBUJ1bm55ElwKLEVuOWdRcjdlcGtpbnVrWFBYU2JaSEVTdlpRS09LWDEya3B4aDRlbitlVH"
        "c9EixZRmF6WWlxTEs5bkVneHpXdHJUeDRCSEd2QkJwZzl1UlhLR0laRkV6a1ZjPQ==";
    std::string hPointShare = "9GBQ+m8cy4qDwvQA/uALl2DzVzJ8+mtr1ZSqHQRBCAA=";
    std::string decryptedRequest =
        "CsIPCpMBCgUxMDA4NhIsQWp5dko5NTN2aUxDenJhdUpoWVlkeHlOWEFmQkZrb3JuczNMbCtSZXdEdz0aXAosMXRPUF"
        "psUWcwOXhrWVh1TVlZK2VuMXhsS1VJUTRNNHRDUUpkVVJpYXBRRT0SLEFadDYzMk1rQmNwOFVzdmRGWHRpb0NZMzdm"
        "NHEwVG5VKzE5OW9nazJ2QWc9EpcBCgZLaXR0ZW4SjAESLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUE9GlwKLDVUdytTdzhjTnVoNUVKektLTzNQMGJtSUZ2b21vaFh1UWhHcExnVzJYQWM9EixnV05OQXhi"
        "cXpnV2RGYlVpUWRlNDZweVVnTmUwdzZYanZ3Ym0wb21uUGdvPRKVAQoERG9nZRKMARIsQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosbWNISm44a0Q4NkNvcXlMR1RSUkloR0YydjRndjNFWUg0bG1Q"
        "QWsrQ1Jndz0SLENKR3RBR3pmVkZqZUtzbngrS2RGaEw3M0tJWHZpc2c1SkhJTkF1VXVFUVU9EpYBCgVCdW5ueRKMAR"
        "IsQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosUzErZERiZldLcTRnbXhYK1ZD"
        "MTBSRDYzSS9qZUJFMFBoazhVS1poRVpBZz0SLHNRTkRSbUVSZWpTbkxvb2FWNFF1N3BKQ3IxNG80ck16aDkvUWs0UE"
        "9Bd2s9EpcBCgZLaXR0ZW4SjAESLGNtVkFUbnBQeWRLOHN2ZG5PSUtxVTU1NWd3WlRpR1p6QTRvKzJKcGpEM2M9GlwK"
        "LFRvZHBFMGg0WkJ5a0UzSldzZzJ2Z3JXYXBXclpNam1EQWVLZ3gzaEpkQXM9EixacnBCQklZR2dqdW9LdGZGQWVUdz"
        "hjenRDRzYzSW5RVGdjSm9ERmQvTWcwPRKVAQoERG9nZRKMARIsU25wMjB4OUw1d2t2RHpzblBHcW1kVDhuQjhBMUM3"
        "WENXaGZPeFd0NE9rQT0aXAoselpjcHhVcS9pSVB1K0w2bFlRcDZXc3JybVhUcGtxWHpCcEIxdVdPYnJRTT0SLElmej"
        "Qxanp0QVo2ZFMzblBUNERzb0NIMVJpQWZ4WjNmajA2YUJjQUU3Z2M9EpYBCgVCdW5ueRKMARIsbU5kTEVNcGw3cTlz"
        "cXJFaVNvdDdMYkdwS2pCUEljdE5SaWRiU01pT2pTST0aXAosaG10bzU3enhpOWNFNStLSm83a2JWOHV0SUFuUDNXck"
        "VDdWxTeHNBVzRRcz0SLEJEb1lDT0R4eHFzNTlaRWhhUVNtL1V5M3dzQ0YwU3NZS2JhMEloNW1vd2c9EpcBCgZLaXR0"
        "ZW4SjAESLC9HQk5QMThTVXNXMGtWVzhMUGxQZ2pZMjc0c0l0VXI0VkF5aE51M1dnSFE9GlwKLCtYV3pxSjlFblVic2"
        "51MUNqV0N2N3ZCQ282WlRTNVJQUFdpalc4UWltZ1k9EixQTXF1enljOEtnazRaVEJtRVJXRDFCSzZ5Nys2eUZUa2k2"
        "eno1L1RJUHdrPRKVAQoERG9nZRKMARIsMXUwTmR4Uk1PN2huRzdTMTBWeTJEWGU1VEgvRko5cTZlTS9CQnA0Y05uWT"
        "0aXAosZXdhZUdhMHc4bnlsSmFDS3JTaGYxQ1kzRWJWZ3cyMlN0OXZ6Z2pUd3Z3az0SLHNhcmJnNFhiRVNzc3kzTFI5"
        "SElQbUlPdjIybitlUUtlSXhVV0grM3ZEUTg9EpYBCgVCdW5ueRKMARIsYkdQam8zUlNaTXZBTmkwUlo0Zi9JVWNQaF"
        "kwNE5GbGVhMWpYRjBOakFSQT0aXAosTXYrWk95OEk3RGtMZjZzVTh4cDIxa2hPbnpmbCtqc0QrTmJSbTAyMDBRWT0S"
        "LDZNQXBsUGRScTdvRkxjQlhLZ0FKQUduYWxkTWlmNXpJSjNmanVQV2xzd009EpcBCgZLaXR0ZW4SjAESLHNudXp1ZG"
        "ZxWGRkU1RTWFZxQ1B4MGJPOU1iMzhJdGFZSzFJNDRaaE53MWc9GlwKLFV4QWJubEVVVm1vYmZVME5RZXpFQ2FYcUwx"
        "M3ljcE9ZeTRtTzRMemIrZ1E9EixkN3hmRnArS3VJUUE2TllTeGdKTUlQdHc2MHFuVUo1UkI1ekdkZlBpV1FjPRKVAQ"
        "oERG9nZRKMARIsc3ZramwyTEIraTFjbXkycGJRSWQvR1EwSGludWhQT0I5YnBLK1VZSGhHcz0aXAosS1ord091ZmlJ"
        "M1pOOVFzMEkwaG5EaXJVSDNmRE5UaGNvOGdkdys5WnB3Zz0SLEV6Q1RaME1NeElLSjFOS1h6UC9nM1BKQjk4WG80YX"
        "lkbWxaK0NibDZkQUE9EpYBCgVCdW5ueRKMARIseUdLOFQrZnl2bzBvbHNselJnb2FFUlZXeWprdEltZ3lDb21CNElC"
        "NkZnRT0aXAosVWVJU3dhNHdkMDd3bEZyMGRGWUpPeis5TU8wNnlLeS8xZGJacjliUjNRND0SLFFwZjE2YnVQSGNOaG"
        "t1ZWdWY1N0TUxBZWtnZFBZUlNuUlVtNjFOdFdVd1k9";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST, systemParameters,
        voteStorage, hPointShare, decryptedRequest);
    // anonymousVotingVerifyCountRequest(string systemParameters, string voteStorage, string
    // hPointShare, string decryptedRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string counterId;
    std::string decryptedResultPartStoragePart;
    abi.abiOut(&out, counterId, decryptedResultPartStoragePart);
    BOOST_TEST(counterId == "10086");
    BOOST_TEST(
        decryptedResultPartStoragePart ==
        "CpMBCgUxMDA4NhIsQWp5dko5NTN2aUxDenJhdUpoWVlkeHlOWEFmQkZrb3JuczNMbCtSZXdEdz0aXAosMXRPUFpsUW"
        "cwOXhrWVh1TVlZK2VuMXhsS1VJUTRNNHRDUUpkVVJpYXBRRT0SLEFadDYzMk1rQmNwOFVzdmRGWHRpb0NZMzdmNHEw"
        "VG5VKzE5OW9nazJ2QWc9EpcBCgZLaXR0ZW4SjAESLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUE9GlwKLDVUdytTdzhjTnVoNUVKektLTzNQMGJtSUZ2b21vaFh1UWhHcExnVzJYQWM9EixnV05OQXhicXpn"
        "V2RGYlVpUWRlNDZweVVnTmUwdzZYanZ3Ym0wb21uUGdvPRKVAQoERG9nZRKMARIsQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosbWNISm44a0Q4NkNvcXlMR1RSUkloR0YydjRndjNFWUg0bG1QQWsr"
        "Q1Jndz0SLENKR3RBR3pmVkZqZUtzbngrS2RGaEw3M0tJWHZpc2c1SkhJTkF1VXVFUVU9EpYBCgVCdW5ueRKMARIsQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosUzErZERiZldLcTRnbXhYK1ZDMTBS"
        "RDYzSS9qZUJFMFBoazhVS1poRVpBZz0SLHNRTkRSbUVSZWpTbkxvb2FWNFF1N3BKQ3IxNG80ck16aDkvUWs0UE9Bd2"
        "s9EpcBCgZLaXR0ZW4SjAESLGNtVkFUbnBQeWRLOHN2ZG5PSUtxVTU1NWd3WlRpR1p6QTRvKzJKcGpEM2M9GlwKLFRv"
        "ZHBFMGg0WkJ5a0UzSldzZzJ2Z3JXYXBXclpNam1EQWVLZ3gzaEpkQXM9EixacnBCQklZR2dqdW9LdGZGQWVUdzhjen"
        "RDRzYzSW5RVGdjSm9ERmQvTWcwPRKVAQoERG9nZRKMARIsU25wMjB4OUw1d2t2RHpzblBHcW1kVDhuQjhBMUM3WENX"
        "aGZPeFd0NE9rQT0aXAoselpjcHhVcS9pSVB1K0w2bFlRcDZXc3JybVhUcGtxWHpCcEIxdVdPYnJRTT0SLElmejQxan"
        "p0QVo2ZFMzblBUNERzb0NIMVJpQWZ4WjNmajA2YUJjQUU3Z2M9EpYBCgVCdW5ueRKMARIsbU5kTEVNcGw3cTlzcXJF"
        "aVNvdDdMYkdwS2pCUEljdE5SaWRiU01pT2pTST0aXAosaG10bzU3enhpOWNFNStLSm83a2JWOHV0SUFuUDNXckVDdW"
        "xTeHNBVzRRcz0SLEJEb1lDT0R4eHFzNTlaRWhhUVNtL1V5M3dzQ0YwU3NZS2JhMEloNW1vd2c9EpcBCgZLaXR0ZW4S"
        "jAESLC9HQk5QMThTVXNXMGtWVzhMUGxQZ2pZMjc0c0l0VXI0VkF5aE51M1dnSFE9GlwKLCtYV3pxSjlFblVic251MU"
        "NqV0N2N3ZCQ282WlRTNVJQUFdpalc4UWltZ1k9EixQTXF1enljOEtnazRaVEJtRVJXRDFCSzZ5Nys2eUZUa2k2eno1"
        "L1RJUHdrPRKVAQoERG9nZRKMARIsMXUwTmR4Uk1PN2huRzdTMTBWeTJEWGU1VEgvRko5cTZlTS9CQnA0Y05uWT0aXA"
        "osZXdhZUdhMHc4bnlsSmFDS3JTaGYxQ1kzRWJWZ3cyMlN0OXZ6Z2pUd3Z3az0SLHNhcmJnNFhiRVNzc3kzTFI5SElQ"
        "bUlPdjIybitlUUtlSXhVV0grM3ZEUTg9EpYBCgVCdW5ueRKMARIsYkdQam8zUlNaTXZBTmkwUlo0Zi9JVWNQaFkwNE"
        "5GbGVhMWpYRjBOakFSQT0aXAosTXYrWk95OEk3RGtMZjZzVTh4cDIxa2hPbnpmbCtqc0QrTmJSbTAyMDBRWT0SLDZN"
        "QXBsUGRScTdvRkxjQlhLZ0FKQUduYWxkTWlmNXpJSjNmanVQV2xzd009EpcBCgZLaXR0ZW4SjAESLHNudXp1ZGZxWG"
        "RkU1RTWFZxQ1B4MGJPOU1iMzhJdGFZSzFJNDRaaE53MWc9GlwKLFV4QWJubEVVVm1vYmZVME5RZXpFQ2FYcUwxM3lj"
        "cE9ZeTRtTzRMemIrZ1E9EixkN3hmRnArS3VJUUE2TllTeGdKTUlQdHc2MHFuVUo1UkI1ekdkZlBpV1FjPRKVAQoERG"
        "9nZRKMARIsc3ZramwyTEIraTFjbXkycGJRSWQvR1EwSGludWhQT0I5YnBLK1VZSGhHcz0aXAosS1ord091ZmlJM1pO"
        "OVFzMEkwaG5EaXJVSDNmRE5UaGNvOGdkdys5WnB3Zz0SLEV6Q1RaME1NeElLSjFOS1h6UC9nM1BKQjk4WG80YXlkbW"
        "xaK0NibDZkQUE9EpYBCgVCdW5ueRKMARIseUdLOFQrZnl2bzBvbHNselJnb2FFUlZXeWprdEltZ3lDb21CNElCNkZn"
        "RT0aXAosVWVJU3dhNHdkMDd3bEZyMGRGWUpPeis5TU8wNnlLeS8xZGJacjliUjNRND0SLFFwZjE2YnVQSGNOaGt1ZW"
        "dWY1N0TUxBZWtnZFBZUlNuUlVtNjFOdFdVd1k9");
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
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

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
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

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
    bytes param = abi.abiIn(API_VERIFY_VOTE_RESULT, systemParameters, voteStorageSum,
        decryptedResultPartStorageSum, voteResultRequest);
    // anonymousVotingVerifyVoteResult(string systemParameters, string voteStorageSum, string
    // decryptedResultPartStorageSum, string voteResultRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));
    u256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorSystemParameters = "123";
    std::string errorVoteStorageSum = "123";
    std::string errorDecryptedResultPartStorageSum = "123";
    std::string errorVoteResultRequest = "123";
    param = abi.abiIn(API_VERIFY_VOTE_RESULT, errorSystemParameters, errorVoteStorageSum,
        errorDecryptedResultPartStorageSum, errorVoteResultRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingGetVoteResultFromRequest)
{
    dev::eth::ContractABI abi;
    std::string voteResultRequest =
        "CkEKHgoaV2VkcHJfdm90aW5nX3RvdGFsX2JhbGxvdHMQPAoKCgZLaXR0ZW4QBgoICgREb2dlEAkKCQoFQnVubnkQDA"
        "==";
    bytes param = abi.abiIn(API_GET_VOTE_RESULT_FROM_REQUEST, voteResultRequest);
    // anonymousVotingGetVoteResultFromRequest(string voteResultRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));
    std::string voteResultStorage;
    abi.abiOut(&out, voteResultStorage);
    BOOST_TEST(
        voteResultStorage ==
        "Ch4KGldlZHByX3ZvdGluZ190b3RhbF9iYWxsb3RzEDwKCgoGS2l0dGVuEAYKCAoERG9nZRAJCgkKBUJ1bm55EAw=");
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
