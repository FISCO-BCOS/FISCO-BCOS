// OpGoldenCorpusProvenanceTest.cpp — golden-corpus provenance tripwire (B3-5).
//
// The replay/gate tests derive all of their judging power from the golden
// corpus being op-geth's output and not this repository's own. The golden
// files carry data keys but no provenance field, so a well-meaning "the
// goldens look stale, let me regenerate them" — done with THIS implementation
// instead of pinned op-geth — leaves every replay assertion passing while the
// gate silently becomes a tautology.
//
// Two independent halves, because neither alone is enough (ported from the
// bcos-evm EngineNewPayloadGateTest.cpp original that the 4c9f240b4 corpus
// move left behind):
//   (a) SHA256SUMS over the golden corpus: catches ANY change to the bytes,
//       whatever produced it, plus set-equality so an unlisted file cannot
//       sneak past unrated. Unlike the original, no hard file-count literals —
//       they rotted twice (12 stale chained sums after 9ab491ab3, 29 files
//       never summed at all); the set invariant subsumes them.
//   (b) the op-geth pin declared inside every vectors/*.json
//       (_op_test_vectors.generator_commit, which the golden ritual never
//       rewrites): ties the corpus those goldens extend to a specific op-geth
//       checkout, machine-readably, instead of to README prose.

#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

// The op-geth checkout the corpus was generated from (tag v1.101702.2), also
// enforced by t8n/generator/regen.sh's PIN at generation time.
constexpr std::string_view c_pinnedOpGethCommit = "e8800cffe53d459cde8a07c8e8f1de9d86e79e07";
constexpr std::string_view c_pinnedGeneratorName = "opt8n-ref";

/// One `<sha256hex>  <relative path>` line of golden/engine/SHA256SUMS.
struct ChecksumEntry
{
    std::string sha256Hex;
    std::string relativePath;
};

std::vector<ChecksumEntry> loadGoldenChecksums()
{
    std::vector<ChecksumEntry> entries;
    std::ifstream in(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "SHA256SUMS");
    BOOST_REQUIRE_MESSAGE(in.is_open(), "cannot open " OP_T8N_GOLDEN_ENGINE_DIR
                                        "/SHA256SUMS — the golden corpus provenance "
                                        "tripwire is missing (B3-5)");
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        // shasum(1) format: 64 hex chars, two spaces, path.
        auto const separator = line.find("  ");
        BOOST_CHECK_MESSAGE(separator == 64U, "malformed SHA256SUMS line: " << line);
        if (separator == std::string::npos)
        {
            continue;
        }
        entries.push_back({line.substr(0, separator), line.substr(separator + 2)});
    }
    return entries;
}

std::string sha256HexOfFile(fs::path const& path)
{
    std::ifstream in(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(in.is_open(), "cannot read " << path.string());
    std::vector<bcos::byte> bytes(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bcos::crypto::hasher::openssl::OpenSSL_SHA2_256_Hasher hasher;
    bcos::h256 digest;
    bcos::crypto::hasher::hash(hasher, bcos::ref(bytes), digest);
    return digest.hex();
}

/// The corpus set SHA256SUMS must cover exactly: top-level *.golden.json plus
/// every chained/*.json (pre/post inputs and goldens — the chained pair's
/// inputs are part of the provenance surface too).
std::set<std::string> corpusFilesOnDisk()
{
    std::set<std::string> onDisk;
    for (auto const& entry : fs::directory_iterator(fs::path(OP_T8N_GOLDEN_ENGINE_DIR)))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json" &&
            entry.path().stem().extension() == ".golden")
        {
            onDisk.insert(entry.path().filename().string());
        }
    }
    auto const chained = fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "chained";
    if (fs::exists(chained))
    {
        for (auto const& entry : fs::directory_iterator(chained))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                onDisk.insert("chained/" + entry.path().filename().string());
            }
        }
    }
    return onDisk;
}

/// jsoncpp's operator[] fabricates nulls; every required provenance field goes
/// through this so a missing member is a named failure, never a silent pass.
Json::Value const& jAt(Json::Value const& doc, char const* key, std::string const& who)
{
    static Json::Value const nullValue;
    BOOST_CHECK_MESSAGE(doc.isMember(key), who << ": missing required field \"" << key << "\"");
    return doc.isMember(key) ? doc[key] : nullValue;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpGoldenCorpusProvenance)

// (a) every golden file matches its recorded checksum, and the listing is
//     complete; (b) every vector declares the pinned op-geth checkout.
BOOST_AUTO_TEST_CASE(GoldenCorpusProvenanceIsPinned)
{
    // ---- (a) checksums over the golden corpus ----
    auto const checksums = loadGoldenChecksums();
    BOOST_REQUIRE_MESSAGE(!checksums.empty(), "SHA256SUMS listed no files");

    std::set<std::string> listed;
    for (auto const& entry : checksums)
    {
        BOOST_TEST_INFO_SCOPE(entry.relativePath);
        listed.insert(entry.relativePath);
        auto const path = fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / entry.relativePath;
        BOOST_CHECK_MESSAGE(
            fs::exists(path), "listed in SHA256SUMS but missing on disk: " << entry.relativePath);
        if (!fs::exists(path))
        {
            continue;
        }
        BOOST_CHECK_MESSAGE(sha256HexOfFile(path) == entry.sha256Hex,
            entry.relativePath << ": golden bytes changed since the op-geth generation ritual. If "
                                  "this is a genuine "
                                  "regeneration, it must come from pinned op-geth "
                               << c_pinnedOpGethCommit
                               << " (README's ritual), and SHA256SUMS must be refreshed in the "
                                  "same commit — never "
                                  "from this repository's own implementation, which would turn the "
                                  "whole gate into a "
                                  "tautology.");
    }

    // The listing must also be COMPLETE: an unlisted golden file would be
    // judged by nothing.
    BOOST_CHECK_MESSAGE(corpusFilesOnDisk() == listed,
        "the set of golden files on disk differs from the set SHA256SUMS covers (new corpus file "
        "without a sum, or a sum for a deleted file)");

    // ---- (b) the corpus these goldens extend declares the pinned op-geth ----
    std::size_t vectorCount = 0;
    for (auto const& entry : fs::directory_iterator(fs::path(OP_T8N_VECTORS_DIR)))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        ++vectorCount;
        BOOST_TEST_INFO_SCOPE(entry.path().filename().string());
        Json::Value doc;
        std::ifstream in(entry.path());
        BOOST_CHECK_MESSAGE(Json::Reader{}.parse(in, doc, false),
            "malformed vector json: " << entry.path().string());
        if (!doc.isObject())
        {
            continue;
        }
        // By value, not const&: gcc-14's -Werror=dangling-reference cannot see
        // that jAt's returned reference binds into `doc` (long-lived) rather
        // than to the temporary string in the same expression.
        Json::Value provenance = jAt(doc, "_op_test_vectors", entry.path().filename().string());
        if (provenance.isNull())
        {
            continue;
        }
        BOOST_CHECK_EQUAL(
            jAt(provenance, "generator_commit", entry.path().filename().string()).asString(),
            std::string(c_pinnedOpGethCommit));
        BOOST_CHECK_EQUAL(jAt(provenance, "generator", entry.path().filename().string()).asString(),
            std::string(c_pinnedGeneratorName));
    }
    // The vectors are tracked alongside the corpus; an empty sweep means a
    // broken checkout, not a provenance pass.
    BOOST_CHECK_MESSAGE(vectorCount > 0, "no vectors/*.json found under " OP_T8N_VECTORS_DIR
                                         " — broken checkout or unfilled "
                                         "regen; the pin check swept nothing");
}

BOOST_AUTO_TEST_SUITE_END()
