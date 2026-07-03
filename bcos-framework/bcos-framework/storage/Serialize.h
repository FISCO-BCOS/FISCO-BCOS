#pragma once

#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>

namespace bcos::storage::serialize
{

// ─── Archive flags (moved from Entry.h) ────────────────────────────
// no_header | no_codecvt | no_tracking for minimal binary output.
constexpr static int32_t ARCHIVE_FLAG =
    boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking;

// ─── boost::archive-based encode/decode ────────────────────────────
// Replaces Entry::setObject() / Entry::getObject().
// Used for types like SystemConfigEntry, ConsensusNodeList, std::vector<std::string>, etc.
// Caller: entry.set(serialize::encode(value));
//         auto val = serialize::decode<T>(entry.get());

template <typename T, typename OutputArchive = boost::archive::binary_oarchive,
    int flag = ARCHIVE_FLAG>
std::string encode(const T& input)
{
    std::string value;
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(value);
    OutputArchive archive(outputStream, flag);
    archive << input;
    outputStream.flush();
    return value;
}

template <typename T, typename InputArchive = boost::archive::binary_iarchive,
    int flag = ARCHIVE_FLAG>
T decode(std::string_view view)
{
    T out;
    boost::iostreams::stream<boost::iostreams::array_source> inputStream(view.data(), view.size());
    InputArchive archive(inputStream, flag);
    archive >> out;
    return out;
}

}  // namespace bcos::storage::serialize

// ─── boost::serialization support for std::tuple ───────────────────
// Required by SystemConfigEntry = std::tuple<std::string, BlockNumber>
// and other tuple-based types stored via boost::archive.
// Moved from Entry.h.
namespace boost::serialization
{
template <typename Archive, typename... Types>
void serialize(Archive& ar, std::tuple<Types...>& t, const unsigned int)
{
    std::apply([&](auto&... element) { ((ar & element), ...); }, t);
}
}  // namespace boost::serialization
