#pragma once
#include "../libutilities/Common.h"
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/serialization/vector.hpp>
#include <string>
#include <vector>

namespace bcos::ledger
{
struct ConsensusNode
{
    ConsensusNode(){};
    ConsensusNode(std::string _nodeID, u256 _weight, std::string _type, std::string _enableNumber)
      : nodeID(std::move(_nodeID)),
        weight(_weight),
        type(std::move(_type)),
        enableNumber(std::move(_enableNumber))
    {}

    std::string nodeID;
    u256 weight;
    std::string type;
    std::string enableNumber;

    template <typename Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        boost::ignore_unused(version);
        ar& nodeID;
        ar& weight;
        ar& type;
        ar& enableNumber;
    }
};

using ConsensusNodeList = std::vector<ConsensusNode>;

inline ConsensusNodeList decodeConsensusList(const std::string_view& value)
{
    boost::iostreams::stream<boost::iostreams::array_source> inputStream(
        value.data(), value.size());
    boost::archive::binary_iarchive archive(inputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    ConsensusNodeList consensusList;
    archive >> consensusList;

    return consensusList;
}

inline std::string encodeConsensusList(const ConsensusNodeList& consensusList)
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