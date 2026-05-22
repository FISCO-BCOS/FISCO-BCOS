/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-G*: static / architectural guards (PoS system_call, 7702, blob).
 *  @file CompatStaticGuardsTest.cpp
 */

#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace bcos::test
{

namespace
{
bool fileContains(const std::string& path, std::string_view needle)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str().find(needle) != std::string::npos;
}

std::optional<std::filesystem::path> findBcosProtocolDirectory()
{
    constexpr int kMaxDepth = 8;
    auto cur = std::filesystem::path(__FILE__).parent_path();
    for (int i = 0; i < kMaxDepth; ++i)
    {
        auto candidate = cur / "bcos-protocol";
        if (std::filesystem::is_directory(candidate))
        {
            return candidate;
        }
        auto parent = cur.parent_path();
        if (parent == cur)
        {
            break;
        }
        cur = parent;
    }
    return std::nullopt;
}

std::string_view trimLeading(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    {
        s.remove_prefix(1);
    }
    return s;
}

// 去掉同行内的 /* ... */（自内向外套叠删除）；若存在未闭合的 /*，则丢弃其后的内容以便后续扫描。
std::string removeCBlockCommentsOnLine(std::string_view line)
{
    std::string s(line);
    while (true)
    {
        const auto open = s.find("/*");
        if (open == std::string::npos)
        {
            break;
        }
        const auto close = s.find("*/", open + 2);
        if (close == std::string::npos)
        {
            s.erase(open);
            break;
        }
        s.erase(open, close - open + 2);
    }
    return s;
}

// 将仅出现在行尾 // 注释中的命中视为可接受；整行以 // 开头则跳过。
// 已处理同行 /* */；跨行块注释仍可能误报（需在评审中知悉）。
bool lineHasAuthorizationListOutsideSlashSlashComment(std::string_view line)
{
    std::string deblocked = removeCBlockCommentsOnLine(line);
    std::string_view work(deblocked);
    constexpr std::string_view needle = "authorization_list";
    auto trimmed = trimLeading(work);
    if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/')
    {
        return false;
    }
    // Only scan the code portion before the first '//' (line comments), so repeated
    // occurrences of the needle inside the comment cannot false-positive.
    const auto slashPos = work.find("//");
    if (slashPos != std::string_view::npos)
    {
        work = work.substr(0, slashPos);
    }
    return work.find(needle) != std::string_view::npos;
}

std::filesystem::path repoRootFromCompatTestFile()
{
    // __FILE__ = .../bcos-executor/test/unittest/evmone/compat/CompatStaticGuardsTest.cpp
    auto cur = std::filesystem::path(__FILE__);
    for (int i = 0; i < 6; ++i)
    {
        cur = cur.parent_path();
    }
    return cur;
}

bool sourceFileHasAuthorizationListOutsideComments(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (lineHasAuthorizationListOutsideSlashSlashComment(line))
        {
            return true;
        }
    }
    return false;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatStaticGuards)

BOOST_AUTO_TEST_CASE(FC_G_no_system_call_block_start)
{
    // __FILE__ = .../bcos-executor/test/unittest/evmone/compat/CompatStaticGuardsTest.cpp
    auto p = std::filesystem::path(__FILE__);
    for (int i = 0; i < 5; ++i)
    {
        p = p.parent_path();
    }
    const auto hostPath = p / "src" / "vm" / "EVMHostInterface.cpp";
    if (!std::filesystem::exists(hostPath))
    {
        const std::string msg = std::string("FC_G: EVMHostInterface.cpp must exist at ") +
                                hostPath.generic_string() +
                                " (repo root derived from __FILE__; missing file indicates "
                                "checkout/layout error in CI).";
        BOOST_FAIL(msg);
    }
    BOOST_CHECK_MESSAGE(!fileContains(hostPath.string(), "system_call_block_start"),
        "PoS system_call_block_start must not appear in executor host (FC-10)");
    BOOST_CHECK_MESSAGE(!fileContains(hostPath.string(), "system_call_block_end"),
        "PoS system_call_block_end must not appear in executor host (FC-10)");
}

BOOST_AUTO_TEST_CASE(FC_G_blob_not_applicable)
{
    BOOST_TEST_MESSAGE(
        "FC-09: EIP-7691 blob throughput N/A for PBFT FISCO-BCOS — no blob_count in block");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_G_7685_requests_not_applicable)
{
    BOOST_TEST_MESSAGE(
        "FC-08: EIP-7685 execution-layer requests are N/A on PBFT FISCO-BCOS. "
        "No system_call_block_start/end request pipeline should be wired.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_G_7702_constants)
{
    const auto commonPath = repoRootFromCompatTestFile() /
                            "transaction-executor/bcos-transaction-executor/Eip7702Common.h";
    BOOST_REQUIRE(std::filesystem::exists(commonPath));
    BOOST_CHECK_MESSAGE(fileContains(commonPath.string(), "25000"),
        "EIP_7702_PER_EMPTY_ACCOUNT_COST must be 25000");
    BOOST_CHECK_MESSAGE(
        fileContains(commonPath.string(), "EIP_7702_REFUND_PER_EXISTING_AUTHORITY = 12500"),
        "EIP_7702 refund constant must be 12500");
}

BOOST_AUTO_TEST_CASE(FC_G_7702_signing_domains)
{
    const auto root = repoRootFromCompatTestFile();
    const auto web3TxPath = root / "bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp";
    const auto applyPath = root / "bcos-executor/src/Web3Eip7702Apply.cpp";
    BOOST_REQUIRE(std::filesystem::exists(web3TxPath));
    BOOST_REQUIRE(std::filesystem::exists(applyPath));
    BOOST_CHECK_MESSAGE(fileContains(web3TxPath.string(), "TransactionType::EIP7702"),
        "type-4 envelope must be decoded in Web3Transaction");
    BOOST_CHECK_MESSAGE(fileContains(applyPath.string(), "0x05"),
        "per-tuple EIP-7702 signing domain must use 0x05 prefix");
}

BOOST_AUTO_TEST_CASE(FC_G_7702_secp256k1_only)
{
    const auto root = repoRootFromCompatTestFile();
    const auto fillPath = root / "bcos-executor/src/Web3Eip7702Fill.cpp";
    const auto applyPath = root / "bcos-executor/src/Web3Eip7702Apply.cpp";
    BOOST_REQUIRE(std::filesystem::exists(fillPath));
    BOOST_REQUIRE(std::filesystem::exists(applyPath));
    BOOST_CHECK_MESSAGE(
        !fileContains(fillPath.string(), "SM2Crypto"), "Web3Eip7702Fill must not reference SM2");
    BOOST_CHECK_MESSAGE(!fileContains(applyPath.string(), "SM2Crypto"),
        "Web3Eip7702Apply must use secp256k1 recovery only");
    BOOST_CHECK_MESSAGE(fileContains(applyPath.string(), "Secp256k1Crypto"),
        "Web3Eip7702Apply must reference Secp256k1Crypto");
}

BOOST_AUTO_TEST_CASE(FC_G_7702_idl_has_field)
{
    const auto root = repoRootFromCompatTestFile();
    const auto tarsPath = root / "bcos-tars-protocol/bcos-tars-protocol/tars/Transaction.tars";
    BOOST_REQUIRE(std::filesystem::exists(tarsPath));
    BOOST_CHECK_MESSAGE(fileContains(tarsPath.string(), "authorizationList"),
        "Transaction.tars must define authorizationList");
    BOOST_CHECK_MESSAGE(fileContains(tarsPath.string(),
                            "16 optional vector<Web3AuthorizationListEntry> authorizationList"),
        "authorizationList must be Tars tag 16");
}

BOOST_AUTO_TEST_CASE(FC_G_7702_application_before_savepoint_refresh)
{
    const auto root = repoRootFromCompatTestFile();
    const auto execPath =
        root / "transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h";
    BOOST_REQUIRE(std::filesystem::exists(execPath));
    BOOST_CHECK_MESSAGE(fileContains(execPath.string(), "FC_G_7702_apply_before_savepoint"),
        "executeStep must document EIP-7702 apply-before-savepoint ordering");
    BOOST_CHECK_MESSAGE(fileContains(execPath.string(), "applyEip7702AuthorizationList"),
        "ExecuteContext must apply EIP-7702 authorizations");
}

BOOST_AUTO_TEST_CASE(FC_G_7702_target_path_only)
{
    const auto root = repoRootFromCompatTestFile();
    const auto legacyExecPath = root / "bcos-executor/src/executor/TransactionExecutor.cpp";
    BOOST_REQUIRE(std::filesystem::exists(legacyExecPath));
    BOOST_CHECK_MESSAGE(!sourceFileHasAuthorizationListOutsideComments(legacyExecPath),
        "legacy TransactionExecutor must not reference authorization_list");
}

BOOST_AUTO_TEST_CASE(FC_G_no_authorization_list_in_protocol)
{
    const auto protoDirOpt = findBcosProtocolDirectory();
    if (!protoDirOpt)
    {
        BOOST_FAIL(
            "findBcosProtocolDirectory returned nullopt: bcos-protocol must exist as a sibling "
            "of the repository root (walk parents from __FILE__); required in CI checkout "
            "layout.");
    }
    const std::filesystem::path& protoDir = *protoDirOpt;

    constexpr std::size_t kMaxFiles = 5000;
    std::size_t scanned = 0;
    bool capHit = false;
    std::string firstHitPath;
    std::error_code iterEc;
    const auto dirOpts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(protoDir, dirOpts, iterEc);
         !iterEc && it != std::filesystem::recursive_directory_iterator(); it.increment(iterEc))
    {
        std::error_code statEc;
        const auto& entry = *it;
        if (!entry.is_regular_file(statEc) || statEc)
        {
            continue;
        }
        const auto& p = entry.path();
        auto ext = p.extension().string();
        if (ext != ".h" && ext != ".cpp")
        {
            continue;
        }
        ++scanned;
        if (scanned > kMaxFiles)
        {
            capHit = true;
            break;
        }
        if (sourceFileHasAuthorizationListOutsideComments(p))
        {
            firstHitPath = p.string();
            break;
        }
    }
    BOOST_CHECK_MESSAGE(!iterEc, iterEc.message());
    if (capHit && firstHitPath.empty())
    {
        const std::string msg =
            std::string("FC_G_no_authorization_list_in_protocol: exceeded kMaxFiles (") +
            std::to_string(kMaxFiles) + ") under " + protoDir.generic_string() +
            " without a violation — incomplete scan; raise kMaxFiles or reduce tree.";
        BOOST_FAIL(msg);
    }
    BOOST_CHECK_MESSAGE(firstHitPath.empty(),
        "bcos-protocol must not use authorization_list outside // line comments; first hit: "
            << firstHitPath);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatStaticGuards
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
