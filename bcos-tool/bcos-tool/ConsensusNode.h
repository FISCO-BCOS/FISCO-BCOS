#pragma once
#include <bcos-utilities/Common.h>
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/version.hpp>
#include <string>
#include <vector>

namespace bcos::ledger
{
struct ConsensusNode
{
    std::string nodeID;
    u256 voteWeight;
    std::string type;
    std::string enableNumber;

    template <typename Archive>
    void serialize(Archive& archive, [[maybe_unused]] unsigned int version)
    {
        archive & nodeID;
        archive & voteWeight;
        archive & type;
        archive & enableNumber;
    }
};

using ConsensusNodeList = std::vector<ledger::ConsensusNode>;
inline ConsensusNodeList decodeConsensusList(const std::string_view& value)
{
    boost::iostreams::stream<boost::iostreams::array_source> inputStream(
        value.data(), value.size());
    boost::archive::binary_iarchive archive(inputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    bcos::ledger::ConsensusNodeList consensusList;
    archive >> consensusList;

    return consensusList;
}

inline std::string encodeConsensusList(const bcos::ledger::ConsensusNodeList& consensusList)
{
    std::string value;
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(value);
    boost::archive::binary_oarchive archive(outputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    archive << consensusList;
    outputStream.flush();

    return value;
}

}  // namespace bcos::ledger